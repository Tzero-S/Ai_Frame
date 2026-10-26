#include "hotkey/hotkey_manager.h"

namespace aim {

void ActivationState::SetMode(ActivationMode m) {
    configured_ = m;
    Reset();
}

void ActivationState::Reset() {
    prev_primary_ = false;
    toggled_ = false;
    armed_ = false;
    effective_ = (configured_ == ActivationMode::Switch) ? ActivationMode::Hold : configured_;
}

bool ActivationState::Update(bool primary_down, bool secondary_down) {
    const bool rising = primary_down && !prev_primary_;

    if (rising) {
        if (configured_ == ActivationMode::Toggle) {
            toggled_ = !toggled_;
        } else if (configured_ == ActivationMode::Switch) {
            effective_ = (effective_ == ActivationMode::Hold) ? ActivationMode::Toggle
                                                             : ActivationMode::Hold;
            toggled_ = (effective_ == ActivationMode::Toggle);
        }
    }
    prev_primary_ = primary_down;

    const bool primary_state = (effective_ == ActivationMode::Hold) ? primary_down : toggled_;
    armed_ = primary_state || secondary_down;
    return armed_;
}

} // namespace aim
