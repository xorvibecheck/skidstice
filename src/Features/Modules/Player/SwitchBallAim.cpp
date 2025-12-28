#include "SwitchBallAim.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Events/RenderEvent.hpp>

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Actor/GameMode.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

#include <Utils/GameUtils/ActorUtils.hpp>
#include <cfloat>

#include <SDK/Minecraft/Network/Packets/MobEquipmentPacket.hpp>

void SwitchBallAim::onEnable() {
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &SwitchBallAim::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &SwitchBallAim::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->listen<RenderEvent, &SwitchBallAim::onRenderEvent>(this);
    mShouldAim = false;
    mTarget = nullptr;
    mLastThrow = 0;
    mLastSlot = -1;
}

void SwitchBallAim::onDisable() {
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &SwitchBallAim::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &SwitchBallAim::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->deafen<RenderEvent, &SwitchBallAim::onRenderEvent>(this);
    mShouldAim = false;
    mTarget = nullptr;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (player && mLastSlot != -1) {
        player->getSupplies()->mSelectedSlot = mLastSlot;
    }
}

void SwitchBallAim::onBaseTickEvent(BaseTickEvent& event) {
    auto player = event.mActor;
    if (!player) {
        mShouldAim = false;
        mTarget = nullptr;
        return;
    }

    if (mOnlyWithSnowball.mValue && !hasSnowballInHand(player)) {
        mShouldAim = false;
        mTarget = nullptr;
        return;
    }

    mTarget = pickBestTarget(player);
    if (!mTarget) {
        mShouldAim = false;
        return;
    }

    mAimRots = computeAimRots(player, mTarget);
    mShouldAim = true;

    if (mAutoThrow.mValue) {
        int64_t throwDelayMs = static_cast<int64_t>(mThrowDelay.mValue * 50.0f);
        if (NOW - mLastThrow >= throwDelayMs) {
            int slot = findSnowballSlot(player, mHotbarOnly.mValue);
            if (slot != -1) {
                auto supplies = player->getSupplies();
                auto container = supplies->getContainer();
                int old = supplies->mSelectedSlot;
                if (mLastSlot == -1) mLastSlot = old;

                switch (mSwitchMode.mValue) {
                    case SwitchMode::None:
                        if (old == slot) {
                            player->getGameMode()->baseUseItem(container->getItem(slot));
                            mLastThrow = NOW;
                        }
                        break;
                    case SwitchMode::Full:
                        supplies->mSelectedSlot = slot;
                        player->getGameMode()->baseUseItem(container->getItem(slot));
                        supplies->mSelectedSlot = old;
                        mLastThrow = NOW;
                        break;
                    case SwitchMode::Spoof: {
                        supplies->mSelectedSlot = slot;
                        PacketUtils::spoofSlot(slot);
                        player->getGameMode()->baseUseItem(container->getItem(slot));
                        supplies->mSelectedSlot = old;
                        PacketUtils::spoofSlot(old);
                        mLastThrow = NOW;
                        break;
                    }
                }
            }
        }
    }
}

void SwitchBallAim::onPacketOutEvent(PacketOutEvent& event) {
    if (!mShouldAim || !mTarget) return;
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (event.mPacket->getId() == PacketID::PlayerAuthInput) {
        auto pkt = event.getPacket<PlayerAuthInputPacket>();
        pkt->mRot = mAimRots;
        pkt->mYHeadRot = mAimRots.y;
    }
}

void SwitchBallAim::onRenderEvent(RenderEvent& event) {
    if (!mShouldAim || !mTarget) return;
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    auto rot = player->getActorRotationComponent();
    rot->mPitch = mAimRots.x;
    rot->mYaw   = mAimRots.y;
    rot->mOldPitch = mAimRots.x;
    rot->mOldYaw   = mAimRots.y;
}

Actor* SwitchBallAim::pickBestTarget(Actor* self) {
    auto actors = ActorUtils::getActorList(false, true);
    Actor* best = nullptr;
    float bestDist = FLT_MAX;

    for (auto* a : actors) {
        if (!a || a == self) continue;
        if (!a->isPlayer()) continue;

        float dist = a->distanceTo(self);
        if (dist > mRange.as<float>()) continue;
        if (!isInFov(self, a, mFov.as<float>())) continue;

        if (dist < bestDist) {
            bestDist = dist;
            best = a;
        }
    }
    return best;
}

bool SwitchBallAim::isInFov(Actor* self, Actor* other, float fovDeg) {
    glm::vec2 rotsTo = MathUtils::getRots(*self->getPos(), other->getAABB());
    float selfYaw = self->getActorRotationComponent()->mYaw;

    float dy = rotsTo.y - selfYaw;
    dy = MathUtils::wrap(dy, -180.f, 180.f);
    return std::abs(dy) <= (fovDeg * 0.5f);
}

bool SwitchBallAim::hasSnowballInHand(Actor* self) {
    auto supplies = self->getSupplies();
    if (!supplies) return false;
    int slot = supplies->mSelectedSlot;
    auto item = supplies->getContainer()->getItem(slot);
    if (!item || !item->mItem) return false;
    return item->getItem()->mName.contains("snowball");
}

int SwitchBallAim::findSnowballSlot(Actor* self, bool hotbarOnly) {
    auto supplies = self->getSupplies();
    if (!supplies) return -1;
    auto cont = supplies->getContainer();
    int max = hotbarOnly ? 9 : 36;

    for (int i = 0; i < max; i++) {
        auto it = cont->getItem(i);
        if (!it || !it->mItem) continue;
        if (it->getItem()->mName.contains("snowball")) return i;
    }
    return -1;
}

glm::vec2 SwitchBallAim::computeAimRots(Actor* self, Actor* target) {
    glm::vec3 eye = *self->getPos();
    eye.y += 1.62f;

    glm::vec3 tgt = *target->getPos();
    tgt.y += mBodyOffsetY.as<float>();

    if (mPredict.mValue) {
        // simple y += 0.5*g*(t^2), with t ~ distHoriz/velocity -> fall predict ;)
        glm::vec3 d = tgt - eye;
        float horiz = std::sqrt(d.x * d.x + d.z * d.z);
        float v = std::max(0.01f, mVelocity.as<float>());
        float t = horiz / v;
        float drop = 0.5f * mGravity.as<float>() * t * t;
        tgt.y += drop;
    }

    return MathUtils::getRots(eye, tgt);
}
