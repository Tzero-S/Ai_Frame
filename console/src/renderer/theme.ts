import type { ThemeConfig } from 'antd';

// 文档 §5.11.4：白色主题为默认（深色仅作可选变体）
export const lightTheme: ThemeConfig = {
  token: {
    colorPrimary: '#1677FF',
    colorBgLayout: '#F5F7FA',
    colorBgContainer: '#FFFFFF',
    colorBorder: '#E5E8EC',
    colorText: '#1F2329',
    colorTextSecondary: '#646A73',
    colorError: '#F5222D',
    colorWarning: '#FA8C16',
    colorSuccess: '#52C41A',
    borderRadius: 6,
    fontFamily:
      '"Segoe UI Variable","Segoe UI",-apple-system,"PingFang SC","Microsoft YaHei",system-ui,sans-serif',
  },
  components: {
    Card: { borderRadiusLG: 14 },
    Button: { borderRadius: 6 },
  },
};
