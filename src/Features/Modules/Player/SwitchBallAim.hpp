#pragma once

#include <Features/Modules/Module.hpp>
#include <Features/Modules/Setting.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>

class SwitchBallAim : public ModuleBase<SwitchBallAim> {
public:
    enum class SwitchMode {
        None,
        Full,
        Spoof
    };

    NumberSetting mRange   = NumberSetting("Range", "Max distance to target players", 8.0f, 1.0f, 50.0f, 0.1f);
    NumberSetting mFov     = NumberSetting("FOV", "Horizontal field-of-view to pick targets", 90.0f, 5.0f, 360.0f, 1.0f);
    BoolSetting   mOnlyWithSnowball = BoolSetting("Only With Snowball", "Aim only when you hold a snowball", true);

    BoolSetting   mAutoThrow   = BoolSetting("Auto Throw", "Automatically throw when aimed", false);
    NumberSetting mThrowDelay  = NumberSetting("Throw Delay (ticks)", "Delay between throws (ticks)", 3.0f, 0.0f, 20.0f, 0.01f);
    BoolSetting   mHotbarOnly  = BoolSetting("Hotbar Only", "Search snowballs only in hotbar", true);
    EnumSettingT<SwitchMode> mSwitchMode =
        EnumSettingT<SwitchMode>("Switch Mode", "How to switch to snowball", SwitchMode::None, "None", "Full", "Spoof");

    BoolSetting   mPredict   = BoolSetting("Predict Drop", "Account for gravity drop", true);
    NumberSetting mVelocity  = NumberSetting("Velocity", "Initial projectile speed", 1.50f, 0.10f, 5.00f, 0.01f);
    NumberSetting mGravity   = NumberSetting("Gravity", "Projectile gravity", 0.03f, 0.00f, 0.20f, 0.001f);
    NumberSetting mBodyOffsetY = NumberSetting("Aim Height", "Aim offset above target feet", 0.80f, 0.00f, 2.00f, 0.01f);

    SwitchBallAim()
        : ModuleBase("SwitchBallAim", "Aim snowballs at nearby players", ModuleCategory::Player, 0, false) {
        addSettings(
            &mRange, &mFov, &mOnlyWithSnowball,
            &mAutoThrow, &mThrowDelay, &mHotbarOnly, &mSwitchMode,
            &mPredict, &mVelocity, &mGravity, &mBodyOffsetY
        );

        VISIBILITY_CONDITION(mThrowDelay,  mAutoThrow.mValue);
        VISIBILITY_CONDITION(mHotbarOnly,  mAutoThrow.mValue || mSwitchMode.mValue != SwitchMode::None);

        mNames = {
            {Lowercase, "switchballaim"},
            {LowercaseSpaced, "switch ball aim"},
            {Normal, "SwitchBallAim"},
            {NormalSpaced, "Switch Ball Aim"}
        };
    }

    // Hooks
    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onPacketOutEvent(class PacketOutEvent& event);
    void onRenderEvent(class RenderEvent& event);

private:
    bool       mShouldAim = false;
    glm::vec2  mAimRots   = {0.f, 0.f};
    Actor*     mTarget    = nullptr;
    uint64_t   mLastThrow = 0;
    int        mLastSlot  = -1;

    Actor* pickBestTarget(class Actor* self);
    bool   isInFov(class Actor* self, class Actor* other, float fovDeg);
    bool   hasSnowballInHand(class Actor* self);
    int    findSnowballSlot(class Actor* self, bool hotbarOnly);
    glm::vec2 computeAimRots(class Actor* self, class Actor* target);
};
