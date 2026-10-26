#include "capture/dxgi_capture.h"

#include <d3d11.h>
#include <dxgi1_2.h>

#include <cstring>
#include <vector>

#include "core/clock.h"
#include "core/logger.h"

namespace aim::capture {

namespace {
template <typename T>
void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}
} // namespace

struct DxgiCapture::Impl {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGIOutputDuplication* dup = nullptr;
    ID3D11Texture2D* staging = nullptr;
    std::vector<uint8_t> buffer;

    int out_w = 0, out_h = 0;
    int roi_x = 0, roi_y = 0, roi_w = 0, roi_h = 0;
    int monitor = 0;
    int edge = 640;
    // 采集规格：want_w/want_h 是请求的 ROI 尺寸，full_frame=true 表示"整块输出"。
    // 旧实现只保留 edge 并强制正方形，导致 roi.mode=full 在 1920x1080 上只截到
    // 居中的 1080x1080（44% 面积），屏幕两侧的目标永远进不了画面。
    int want_w = 640, want_h = 640;
    bool full_frame = false;
    uint32_t seq = 0;
    bool ready = false;

    void ReleaseAll() {
        SafeRelease(staging);
        SafeRelease(dup);
        SafeRelease(ctx);
        SafeRelease(device);
        buffer.clear();
        ready = false;
    }

    bool CreateDevice() {
        D3D_FEATURE_LEVEL got{};
        // 不显式指定 11_1（部分系统会 E_INVALIDARG），由运行时选最高可用级别
        const HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION,
            &device, &got, &ctx);
        if (FAILED(hr)) {
            AIM_ENGINE(Error, "D3D11CreateDevice failed");
            return false;
        }
        return true;
    }

    bool CreateDuplication(int monitor_index) {
        monitor = monitor_index;

        IDXGIDevice* dxgi_dev = nullptr;
        if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_dev))) return false;

        IDXGIAdapter* adapter = nullptr;
        HRESULT hr = dxgi_dev->GetAdapter(&adapter);
        SafeRelease(dxgi_dev);
        if (FAILED(hr)) return false;

        IDXGIOutput* output = nullptr;
        hr = adapter->EnumOutputs(monitor_index, &output);
        SafeRelease(adapter);
        if (FAILED(hr)) {
            AIM_ENGINE(Error, "DXGI EnumOutputs failed (monitor index?)");
            return false;
        }

        DXGI_OUTPUT_DESC od{};
        output->GetDesc(&od);
        out_w = od.DesktopCoordinates.right - od.DesktopCoordinates.left;
        out_h = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;

        IDXGIOutput1* output1 = nullptr;
        hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
        SafeRelease(output);
        if (FAILED(hr)) return false;

        hr = output1->DuplicateOutput(device, &dup);
        SafeRelease(output1);
        if (FAILED(hr)) {
            AIM_ENGINE(Error, "DXGI DuplicateOutput failed");
            return false;
        }
        return true;
    }

    void ComputeRoi() {
        if (full_frame) {
            // 整块输出：ROI 就是输出自身，不再受"屏幕宽"影响（多屏下也不会截窄）
            roi_w = out_w;
            roi_h = out_h;
            roi_x = 0;
            roi_y = 0;
            return;
        }
        int w = (want_w > 0) ? want_w : ((edge > 0) ? edge : 640);
        int h = (want_h > 0) ? want_h : w; // 未给高 = 正方形（center_square 语义）
        if (w > out_w) w = out_w;
        if (h > out_h) h = out_h;
        roi_w = w;
        roi_h = h;
        roi_x = (out_w - w) / 2;
        roi_y = (out_h - h) / 2;
    }

    bool CreateStaging() {
        SafeRelease(staging);
        D3D11_TEXTURE2D_DESC d{};
        d.Width = static_cast<UINT>(roi_w);
        d.Height = static_cast<UINT>(roi_h);
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.BindFlags = 0;
        d.MiscFlags = 0;
        if (FAILED(device->CreateTexture2D(&d, nullptr, &staging))) return false;
        buffer.resize(static_cast<size_t>(roi_w) * roi_h * 4);
        return true;
    }

    bool Rebuild() {
        SafeRelease(dup);
        if (!CreateDuplication(monitor)) return false;
        ComputeRoi();
        return CreateStaging();
    }
};

DxgiCapture::DxgiCapture() : impl_(std::make_unique<Impl>()) {}
DxgiCapture::~DxgiCapture() { impl_->ReleaseAll(); }

bool DxgiCapture::init(const spi::CaptureSpec& spec) {
    impl_->ReleaseAll();
    impl_->edge = (spec.roi_w > 0) ? spec.roi_w : 640;
    impl_->want_w = (spec.roi_w > 0) ? spec.roi_w : 640;
    impl_->want_h = (spec.roi_h > 0) ? spec.roi_h : impl_->want_w;
    impl_->full_frame = spec.full_frame;

    if (!impl_->CreateDevice()) return false;
    if (!impl_->CreateDuplication(spec.output_monitor)) {
        impl_->ReleaseAll();
        return false;
    }
    impl_->ComputeRoi();
    if (!impl_->CreateStaging()) {
        impl_->ReleaseAll();
        return false;
    }
    impl_->ready = true;
    return true;
}

spi::Frame DxgiCapture::grab(const spi::CaptureSpec&) {
    if (!impl_->ready || !impl_->dup) return spi::Frame::empty();

    DXGI_OUTDUPL_FRAME_INFO info{};
    IDXGIResource* res = nullptr;
    const HRESULT hr = impl_->dup->AcquireNextFrame(static_cast<UINT>(timeout_ms), &info, &res);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return spi::Frame::empty(); // 正常无新帧
    if (hr == DXGI_ERROR_ACCESS_LOST) {                             // 切全屏/最小化
        impl_->Rebuild();
        return spi::Frame::empty();
    }
    if (FAILED(hr)) return spi::Frame::empty();

    ID3D11Texture2D* tex = nullptr;
    const HRESULT hr2 = res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    res->Release();
    if (FAILED(hr2) || !tex) {
        impl_->dup->ReleaseFrame();
        return spi::Frame::empty();
    }

    // GPU 侧裁剪：只拷贝 ROI（§5.2.2）
    D3D11_BOX box{};
    box.left = static_cast<UINT>(impl_->roi_x);
    box.top = static_cast<UINT>(impl_->roi_y);
    box.front = 0;
    box.right = static_cast<UINT>(impl_->roi_x + impl_->roi_w);
    box.bottom = static_cast<UINT>(impl_->roi_y + impl_->roi_h);
    box.back = 1;
    impl_->ctx->CopySubresourceRegion(impl_->staging, 0, 0, 0, 0, tex, 0, &box);
    tex->Release();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(impl_->ctx->Map(impl_->staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        impl_->dup->ReleaseFrame();
        return spi::Frame::empty();
    }

    // 逐行拷贝，尊重 RowPitch
    for (int y = 0; y < impl_->roi_h; ++y) {
        std::memcpy(impl_->buffer.data() + static_cast<size_t>(y) * impl_->roi_w * 4,
                    static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                    static_cast<size_t>(impl_->roi_w) * 4);
    }
    impl_->ctx->Unmap(impl_->staging, 0);
    impl_->dup->ReleaseFrame();

    spi::Frame f;
    f.seq = static_cast<int>(++impl_->seq);
    f.ts = static_cast<uint64_t>(Clock::NowUs());
    f.w = impl_->roi_w;
    f.h = impl_->roi_h;
    f.bgra = impl_->buffer.data();
    f.valid = true;
    f.screen_w = impl_->out_w;
    f.screen_h = impl_->out_h;
    f.src_x = impl_->roi_x;
    f.src_y = impl_->roi_y;
    f.src_w = impl_->roi_w;
    f.src_h = impl_->roi_h;
    return f;
}

const char* DxgiCapture::status() const {
    return impl_->ready ? "dxgi: ready" : "dxgi: not initialized";
}

} // namespace aim::capture
