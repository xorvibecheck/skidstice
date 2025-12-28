#include "AutoMiner.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Events/RenderEvent.hpp>

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/GameMode.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/World/BlockLegacy.hpp>
#include <SDK/Minecraft/World/BlockSource.hpp>
#include <SDK/Minecraft/World/HitResult.hpp>
#include <SDK/Minecraft/World/Level.hpp>

#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

#include <Hook/Hooks/MiscHooks/MouseHook.hpp>
#include <Utils/GameUtils/ChatUtils.hpp>
#include <Utils/GameUtils/ItemUtils.hpp>
#include <Utils/GameUtils/PacketUtils.hpp>
#include <Utils/MiscUtils/BlockUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>

#if __has_include(<SDK/Minecraft/Network/Packets/TextPacket.hpp>)
  #include <SDK/Minecraft/Network/Packets/TextPacket.hpp>
  #define AM_HAVE_TEXTPACKET 1
#else
  #define AM_HAVE_TEXTPACKET 0
#endif

void AutoMiner::onEnable() {
    resetMining();
    gFeatureManager->mDispatcher->listen<BaseTickEvent,  &AutoMiner::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<RenderEvent,    &AutoMiner::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &AutoMiner::onPacketOutEvent>(this);
}

void AutoMiner::onDisable() {
    gFeatureManager->mDispatcher->deafen<BaseTickEvent,  &AutoMiner::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<RenderEvent,    &AutoMiner::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &AutoMiner::onPacketOutEvent>(this);

    if (mHolding) {
        MouseHook::simulateMouseInput(1, 0, 0, 0, 0, 0);
        mHolding = false;
    }

    resetMining();
}

void AutoMiner::onBaseTickEvent(BaseTickEvent& e) {
    auto* player = e.mActor;
    if (!player) return;

    auto* src = ClientInstance::get()->getBlockSource();
    if (!src) return;

    if (mChatOnFullInv && inventoryFull())
        sendFullInvChat();

    planTargets();

    if (mTurnOnBedrock && (isBedrock(mTargetBase) || isBedrock(mTargetAbove))) {
        mShouldTurnLeft = true;
        mActive         = MiningWhich::None;
        mWantHold       = false;
        return;
    }

    Block* b0 = src->getBlock(mTargetBase);
    Block* b1 = src->getBlock(mTargetAbove);

    PlayerInventory* supplies = player->getSupplies();
    if (!supplies) return;
    mPrevSlot = supplies->getmSelectedSlot();

    if (!isClear(mTargetBase)) {
        mActive   = MiningWhich::Base;
        mWantHold = true;

        if (mShouldSpoofSlot && b0) {
            int best = bestToolFor(b0);
            PacketUtils::spoofSlot(best, false);
            mShouldSpoofSlot = false;
            mSpoofedSlot     = best;
        }
        return;
    }

    if (!isClear(mTargetAbove)) {
        mActive   = MiningWhich::Above;
        mWantHold = true;

        if (mShouldSpoofSlot && b1) {
            int best = bestToolFor(b1);
            PacketUtils::spoofSlot(best, false);
            mShouldSpoofSlot = false;
            mSpoofedSlot     = best;
        }
        return;
    }

    mActive         = MiningWhich::None;
    mWantHold       = false;

    if (mShouldSetbackSlot && mSpoofedSlot >= 0 && mPrevSlot >= 0) {
        PacketUtils::spoofSlot(mPrevSlot, false);
        mSpoofedSlot     = -1;
        mShouldSetbackSlot = false;
    }

    mShouldAutoWalk = mAutoWalk.mValue;
}

void AutoMiner::onRenderEvent(RenderEvent&) {
    if (mWantHold) {
        MouseHook::simulateMouseInput(1, 1, 0, 0, 0, 0); // down/hold
        mHolding = true;
        mShouldSetbackSlot = true;
    } else {
        if (mHolding) {
            MouseHook::simulateMouseInput(1, 0, 0, 0, 0, 0); // up
            mHolding = false;
        }
    }

    if (!mRenderGhost.mValue) return;
    if (mTargetBase.x == INT_MAX) return;

    auto drawBox = [](const glm::ivec3& pos, ImU32 color) {
        RenderUtils::drawOutlinedAABB(AABB(glm::vec3(pos), glm::vec3(1,1,1)), true, color);
    };

    const auto colorBase  = isClear(mTargetBase)  ? IM_COL32( 80,255, 80,160) : IM_COL32(255, 80, 80,160);
    const auto colorAbove = isClear(mTargetAbove) ? IM_COL32( 80,255, 80,160) : IM_COL32(255, 80, 80,160);
    drawBox(mTargetBase,  colorBase);
    drawBox(mTargetAbove, colorAbove);
}

void AutoMiner::onPacketOutEvent(PacketOutEvent& e) {
    if (e.mPacket->getId() != PacketID::PlayerAuthInput) return;

    auto* ci = ClientInstance::get();
    if (!ci) return;
    auto* player = ci->getLocalPlayer();
    if (!player) return;

    auto paip = e.getPacket<PlayerAuthInputPacket>();
    if (!paip) return;

    if (mActive != MiningWhich::None) {
        const glm::ivec3 tgt = (mActive == MiningWhich::Base) ? mTargetBase : mTargetAbove;
        auto aabb  = AABB(glm::vec3(tgt), glm::vec3(1,1,1));
        glm::vec2 r = MathUtils::getRots(*player->getPos(), aabb);
        paip->mRot      = r;
        paip->mYHeadRot = r.y;
    }

    if (mShouldTurnLeft) {
        paip->mYHeadRot -= 90.f;
        paip->mRot.y    -= 90.f;
        mShouldTurnLeft  = false;
    }

    if (mShouldAutoWalk) {
        paip->mMove              = {0.f, 1.f};
        paip->mAnalogMoveVector  = {0.f, 1.f};
        paip->addInputData(AuthInputAction::UP);
        mShouldAutoWalk = false;
    }
}

void AutoMiner::planTargets() {
    auto* ci = ClientInstance::get();
    if (!ci) return;
    auto* player = ci->getLocalPlayer();
    if (!player) return;

    const glm::ivec3 feet = playerFeet();
    const glm::ivec3 fwd  = forwardCardinal();
    const int dist        = mForwardDistance.as<int>();

    const int middleY = feet.y + 1;

    int baseY, aboveY;
    if (mOrigin.mValue == TunnelOrigin::FromMiddle) {
        baseY  = middleY - 1;
        aboveY = middleY;
    } else {
        baseY  = feet.y;
        aboveY = feet.y + 1;
    }

    mTargetBase  = feet + fwd * dist;  mTargetBase.y  = baseY;
    mTargetAbove = feet + fwd * dist;  mTargetAbove.y = aboveY;
}

void AutoMiner::resetMining() {
    mTargetBase         = {INT_MAX, INT_MAX, INT_MAX};
    mTargetAbove        = {INT_MAX, INT_MAX, INT_MAX};
    mActive             = MiningWhich::None;

    mShouldSpoofSlot    = true;
    mShouldSetbackSlot  = false;
    mSpoofedSlot        = -1;
    mPrevSlot           = -1;

    mShouldAutoWalk     = false;
    mShouldTurnLeft     = false;
    mWantHold           = false;
    mHolding            = false;
}

bool AutoMiner::isClear(const glm::ivec3& pos) {
    auto* src = ClientInstance::get()->getBlockSource();
    if (!src) return true;
    auto* b = src->getBlock(pos);
    if (!b) return true;
    if (!b->mLegacy) return false;
    return b->mLegacy->isAir();
}

bool AutoMiner::isBedrock(const glm::ivec3& pos) {
    auto* src = ClientInstance::get()->getBlockSource();
    if (!src) return false;
    auto* b = src->getBlock(pos);
    if (!b) return false;
    if (!b->mLegacy) return false;
    return b->mLegacy->getBlockId() == 7;
}

bool AutoMiner::inventoryFull() {
    auto* p = ClientInstance::get()->getLocalPlayer();
    if (!p) return false;

    auto* supplies = p->getSupplies();
    if (!supplies) return false;

    auto* inv = supplies->getContainer();
    if (!inv) return false;

    for (int i = 0; i < 36; ++i) {
        auto* st = inv->getItem(i);
        if (!st || !st->mItem || st->mCount <= 0) {
            return false;
        }
    }

    return true;
}

void AutoMiner::sendFullInvChat() {
    PacketUtils::sendChatMessage("/sellall");
}

glm::ivec3 AutoMiner::forwardCardinal() {
    auto* p = ClientInstance::get()->getLocalPlayer();
    if (!p) return {0,0,1};
    auto hc = p->getActorHeadRotationComponent();
    float yaw = hc ? hc->mHeadRot : 0.f;
    const float r = glm::radians(yaw);
    const float sx = -sinf(r);
    const float sz =  cosf(r);
    glm::ivec3 v{ (int)std::round(sx), 0, (int)std::round(sz) };
    if (v.x == 0 && v.z == 0) v.z = 1;
    return v;
}

glm::ivec3 AutoMiner::playerFeet() {
    auto* ci = ClientInstance::get();
    if (!ci) return {0,0,0};
    auto* p = ci->getLocalPlayer();
    if (!p) return {0,0,0};
    auto* pos = p->getPos();
    if (!pos) return {0,0,0};
    glm::vec3 pp = *pos;
    return { (int)std::floor(pp.x), (int)std::floor(pp.y - 1.0f), (int)std::floor(pp.z) };
}

int AutoMiner::bestToolFor(Block* b) {
    if (!b) return 0;
    int slot = ItemUtils::getBestBreakingTool(b, mHotbarOnly.mValue);
    if (slot < 0) slot = 0;
    return slot;
}
