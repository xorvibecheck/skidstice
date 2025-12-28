#pragma once

#include <Features/Modules/Module.hpp>
#include <glm/vec3.hpp>
#include <cstdint>
#include <climits>

class Block;
class BaseTickEvent;
class RenderEvent;
class PacketOutEvent;

class AutoMiner : public ModuleBase<AutoMiner> {
public:
    enum class TunnelOrigin { FromFeet, FromMiddle };
    enum class MiningWhich   { None, Base, Above };

    EnumSettingT<TunnelOrigin> mOrigin = EnumSettingT<TunnelOrigin>(
        "Tunnel Origin", "Center of the tunnel", TunnelOrigin::FromMiddle,
        "From Feet", "From Middle"
    );
    NumberSetting mForwardDistance = NumberSetting("Forward Dist", "Maximum dist to break blocks", 1, 1, 3, 1);
    BoolSetting   mAutoWalk        = BoolSetting("Auto Walk", "Player walk when the path is good", true);
    BoolSetting   mTurnOnBedrock   = BoolSetting("Turn Left on Bedrock", "Turn left if bedrock in front", true);
    BoolSetting   mChatOnFullInv   = BoolSetting("Chat on Full Inv", "Send a message when the player is full inventory", true);
    BoolSetting   mHotbarOnly      = BoolSetting("Hotbar Only", "Use hotbar tools", true);
    BoolSetting   mSwing           = BoolSetting("Swing", "Do the swing anim", false);
    BoolSetting   mRenderGhost     = BoolSetting("Render Ghost", "Render air green and blocks red", true);

    AutoMiner()
        : ModuleBase("AutoMiner", "Automatically mines for you", ModuleCategory::Player, 0, false) {
        addSettings(&mOrigin, &mForwardDistance, &mAutoWalk, &mTurnOnBedrock, &mChatOnFullInv,
                    &mHotbarOnly, &mSwing, &mRenderGhost);
        mNames = {
            {Lowercase, "autominer"},
            {LowercaseSpaced, "auto miner"},
            {Normal, "AutoMiner"},
            {NormalSpaced, "Auto Miner"},
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(BaseTickEvent& e);
    void onRenderEvent(RenderEvent& e);
    void onPacketOutEvent(PacketOutEvent& e);

private:
    glm::ivec3  mTargetBase   = {INT_MAX, INT_MAX, INT_MAX};
    glm::ivec3  mTargetAbove  = {INT_MAX, INT_MAX, INT_MAX};
    MiningWhich mActive       = MiningWhich::None;

    bool mShouldSpoofSlot     = true;
    bool mShouldSetbackSlot   = false;
    int  mSpoofedSlot         = -1;
    int  mPrevSlot            = -1;

    bool mShouldAutoWalk      = false;
    bool mShouldTurnLeft      = false;
    bool mWantHold            = false;
    bool mHolding             = false;

    void planTargets();
    void resetMining();

    bool isClear (const glm::ivec3& pos);
    bool isBedrock(const glm::ivec3& pos);
    bool inventoryFull();
    void sendFullInvChat();

    glm::ivec3 forwardCardinal();
    glm::ivec3 playerFeet();
    int  bestToolFor(Block* b);
};
