//
// Created by vastrakai on 7/6/2024.
//

#include "InvManager.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Events/PingUpdateEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <SDK/Minecraft/Network/MinecraftPackets.hpp>
#include <SDK/Minecraft/Network/Packets/InteractPacket.hpp>
#include <SDK/Minecraft/Network/Packets/ContainerClosePacket.hpp>
#include <SDK/Minecraft/World/BlockLegacy.hpp>

static const char* ItemTypeName(SItemType t) {
    switch (t) {
        case SItemType::Helmet:   return "Helmet";
        case SItemType::Chestplate:return "Chestplate";
        case SItemType::Leggings: return "Leggings";
        case SItemType::Boots:    return "Boots";
        case SItemType::Sword:    return "Sword";
        case SItemType::Pickaxe:  return "Pickaxe";
        case SItemType::Axe:      return "Axe";
        case SItemType::Shovel:   return "Shovel";
        default:                  return "Other";
    }
}

static inline bool shouldPauseForUI() {
    auto ci = ClientInstance::get();
    return ci && ci->getMouseGrabbed();
}

static inline bool isHotbarIndex(int idx) {
    return (idx >= 0  && idx <= 8)  ||
           (idx >= 9  && idx <= 17) ||
           (idx >= 27 && idx <= 35);
}

static inline int hotbarBaseOf(int idx) {
    if (idx >= 0  && idx <= 8)  return 0;
    if (idx >= 9  && idx <= 17) return 9;
    if (idx >= 27 && idx <= 35) return 27;
    return -1;
}

static inline int chooseTargetBaseForPref(int fromSlot) {
    int b = hotbarBaseOf(fromSlot);
    return (b >= 0) ? b : 0;
}

void InvManager::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &InvManager::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketInEvent, &InvManager::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &InvManager::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->listen<PingUpdateEvent, &InvManager::onPingUpdateEvent>(this);
}

void InvManager::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &InvManager::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &InvManager::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &InvManager::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->deafen<PingUpdateEvent, &InvManager::onPingUpdateEvent>(this);
}

void InvManager::onBaseTickEvent(BaseTickEvent& event)
{
    auto* player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    auto* supplies  = player->getSupplies();
    if (!supplies) return;

    auto* container = supplies->getContainer();
    if (!container) return;

    auto* armorContainer = player->getArmorContainer();

    if (mManagementMode.mValue != ManagementMode::Always && !mHasOpenContainer)
        return;

    static uint64_t pauseUntil = 0;
    if (ClientInstance::get()->getMouseGrabbed()) pauseUntil = NOW + 300;
    if (NOW < pauseUntil) return;

    const bool isInstant = (mMode.mValue == Mode::Instant);
    if (mLastAction + static_cast<uint64_t>(mDelay.mValue) > NOW) return;

    const int selectedHotbar = supplies->mSelectedSlot;
    const int protectedSel0  = 0  + selectedHotbar;
    const int protectedSel9  = 9  + selectedHotbar;
    const int protectedSel27 = 27 + selectedHotbar;

    int bestHelmetSlot = -1, bestChestplateSlot = -1, bestLeggingsSlot = -1, bestBootsSlot = -1;
    int bestSwordSlot = -1, bestPickaxeSlot = -1, bestAxeSlot = -1, bestShovelSlot = -1;

    int bestHelmetValue = 0, bestChestplateValue = 0, bestLeggingsValue = 0, bestBootsValue = 0;
    int bestSwordValue = 0, bestPickaxeValue = 0, bestAxeValue = 0, bestShovelValue = 0;

    int equippedHelmetValue     = ItemUtils::getItemValue(armorContainer->getItem(0));
    int equippedChestplateValue = ItemUtils::getItemValue(armorContainer->getItem(1));
    int equippedLeggingsValue   = ItemUtils::getItemValue(armorContainer->getItem(2));
    int equippedBootsValue      = ItemUtils::getItemValue(armorContainer->getItem(3));

    int firstBowSlot = -1;
    int fireSwordSlot = ItemUtils::getFireSword(false);

    for (int i = 0; i < 36; i++) {
        ItemStack* st = container->getItem(i);
        if (!st || !st->mItem) continue;

        auto* it = st->getItem();
        auto type = it->getItemType();

        if (it->mName.contains("bow") && mDropExtraBows.mValue) {
            if (firstBowSlot == -1) {
                firstBowSlot = i;
            } else {
                if (i == protectedSel0 || i == protectedSel9 || i == protectedSel27) {
                } else {
                    spdlog::info("[InvManager] Drop extra bow slot {}", i);
                    container->dropSlot(i);
                    mLastAction = NOW;
                    if (!isInstant) return;
                }
                continue;
            }
        }

        if (mIgnoreFireSword.mValue && fireSwordSlot != -1 && fireSwordSlot == i) continue;

        int val = ItemUtils::getItemValue(st);

        if (type == SItemType::Helmet && val > bestHelmetValue) {
            if (equippedHelmetValue >= val) { bestHelmetSlot = -1; continue; }
            bestHelmetSlot = i; bestHelmetValue = val;
        } else if (type == SItemType::Chestplate && val > bestChestplateValue) {
            if (equippedChestplateValue >= val) { bestChestplateSlot = -1; continue; }
            bestChestplateSlot = i; bestChestplateValue = val;
        } else if (type == SItemType::Leggings && val > bestLeggingsValue) {
            if (equippedLeggingsValue >= val) { bestLeggingsSlot = -1; continue; }
            bestLeggingsSlot = i; bestLeggingsValue = val;
        } else if (type == SItemType::Boots && val > bestBootsValue) {
            if (equippedBootsValue >= val) { bestBootsSlot = -1; continue; }
            bestBootsSlot = i; bestBootsValue = val;
        } else if (type == SItemType::Sword && val > bestSwordValue) {
            bestSwordSlot = i; bestSwordValue = val;
        } else if (type == SItemType::Pickaxe && val > bestPickaxeValue) {
            bestPickaxeSlot = i; bestPickaxeValue = val;
        } else if (type == SItemType::Axe && val > bestAxeValue) {
            bestAxeSlot = i; bestAxeValue = val;
        } else if (type == SItemType::Shovel && val > bestShovelValue) {
            bestShovelSlot = i; bestShovelValue = val;
        }
    }

    spdlog::info("[InvManager] Best H:{} C:{} L:{} B:{} | Sw:{} P:{} A:{} Sh:{} | selHotbar:{}",
        bestHelmetSlot, bestChestplateSlot, bestLeggingsSlot, bestBootsSlot,
        bestSwordSlot, bestPickaxeSlot, bestAxeSlot, bestShovelSlot, selectedHotbar);

    std::vector<int> toDrop;
    for (int i = 0; i < 36; i++) {
        ItemStack* st = container->getItem(i);
        if (!st || !st->mItem) continue;

        if (i == protectedSel0 || i == protectedSel9 || i == protectedSel27) continue;

        if (mIgnoreFireSword.mValue && fireSwordSlot != -1 && fireSwordSlot == i) continue;

        auto* it = st->getItem();
        auto type = it->getItemType();
        bool hasFP = st->getEnchantValue(Enchant::FIRE_PROTECTION) > 0;
        if (mStealFireProtection.mValue && hasFP) continue;

        if (type == SItemType::Sword       && i != bestSwordSlot)        toDrop.push_back(i);
        else if (type == SItemType::Pickaxe   && i != bestPickaxeSlot)   toDrop.push_back(i);
        else if (type == SItemType::Axe       && i != bestAxeSlot)       toDrop.push_back(i);
        else if (type == SItemType::Shovel    && i != bestShovelSlot)    toDrop.push_back(i);
        else if (type == SItemType::Helmet    && i != bestHelmetSlot)    { if (!(mStealFireProtection.mValue && hasFP)) toDrop.push_back(i); }
        else if (type == SItemType::Chestplate&& i != bestChestplateSlot){ if (!(mStealFireProtection.mValue && hasFP)) toDrop.push_back(i); }
        else if (type == SItemType::Leggings  && i != bestLeggingsSlot)  { if (!(mStealFireProtection.mValue && hasFP)) toDrop.push_back(i); }
        else if (type == SItemType::Boots     && i != bestBootsSlot)     { if (!(mStealFireProtection.mValue && hasFP)) toDrop.push_back(i); }
    }

    for (int s : toDrop) {
        spdlog::info("[InvManager] Drop slot {}", s);
        container->dropSlot(s);
        mLastAction = NOW;
        if (!isInstant) return;
    }

    auto doSwapToPref = [&](int& fromSlot, int prefSlot1to9, const char* tag) -> bool {
        if (prefSlot1to9 <= 0) return false;
        if (fromSlot == -1)    return false;

        int base = -1;
        if (fromSlot >= 0 && fromSlot <= 8) base = 0;
        else if (fromSlot >= 9 && fromSlot <= 17) base = 9;
        else if (fromSlot >= 27 && fromSlot <= 35) base = 27;
        else base = 0;

        int toSlot = base + (prefSlot1to9 - 1);

        if (toSlot == protectedSel0 || toSlot == protectedSel9 || toSlot == protectedSel27) {
            spdlog::info("[InvManager] Skip swap {} -> selected-protected {}", tag, toSlot);
            return false;
        }

        if (fromSlot == toSlot) return false;

        spdlog::info("[InvManager] Swap {} {} -> {} (base {})", tag, fromSlot, toSlot, base);
        container->swapSlots(fromSlot, toSlot);
        fromSlot = toSlot;
        mLastAction = NOW;
        return true;
    };

    if (mPreferredSlots.mValue) {
        if (!isInstant && (
            doSwapToPref(bestSwordSlot,    mPreferredSwordSlot.mValue,   "Sword")   ||
            doSwapToPref(bestPickaxeSlot,  mPreferredPickaxeSlot.mValue, "Pickaxe") ||
            doSwapToPref(bestAxeSlot,      mPreferredAxeSlot.mValue,     "Axe")     ||
            doSwapToPref(bestShovelSlot,   mPreferredShovelSlot.mValue,  "Shovel")  ||
            (mPreferredFireSwordSlot.mValue!=0 && fireSwordSlot!=-1 && fireSwordSlot!=bestSwordSlot &&
             doSwapToPref(fireSwordSlot,   mPreferredFireSwordSlot.mValue, "FireSword"))
        )) return;

        doSwapToPref(bestSwordSlot,    mPreferredSwordSlot.mValue,   "Sword");
        doSwapToPref(bestPickaxeSlot,  mPreferredPickaxeSlot.mValue, "Pickaxe");
        doSwapToPref(bestAxeSlot,      mPreferredAxeSlot.mValue,     "Axe");
        doSwapToPref(bestShovelSlot,   mPreferredShovelSlot.mValue,  "Shovel");
        if (mPreferredFireSwordSlot.mValue!=0 && fireSwordSlot!=-1 && fireSwordSlot!=bestSwordSlot)
            doSwapToPref(fireSwordSlot, mPreferredFireSwordSlot.mValue, "FireSword");

        if (mPreferredBlocksSlot.mValue != 0) {
            int pref = mPreferredBlocksSlot.mValue;
            int firstPlaceable = ItemUtils::getFirstPlaceable(false);
            int base = 0;
            if (firstPlaceable >= 9 && firstPlaceable <= 17) base = 9;
            else if (firstPlaceable >= 27 && firstPlaceable <= 35) base = 27;

            if (firstPlaceable != -1) {
                int toSlot = base + (pref - 1);
                if (toSlot != protectedSel0 && toSlot != protectedSel9 && toSlot != protectedSel27 &&
                    firstPlaceable != toSlot) {
                    spdlog::info("[InvManager] Swap Blocks {} -> {} (base {})", firstPlaceable, toSlot, base);
                    container->swapSlots(firstPlaceable, toSlot);
                    mLastAction = NOW;
                    if (!isInstant) return;
                }
            }
        }
    }

    std::vector<int> toEquip;
    if (bestHelmetSlot != -1)      toEquip.push_back(bestHelmetSlot);
    if (bestChestplateSlot != -1)  toEquip.push_back(bestChestplateSlot);
    if (bestLeggingsSlot != -1)    toEquip.push_back(bestLeggingsSlot);
    if (bestBootsSlot != -1)       toEquip.push_back(bestBootsSlot);

    for (int s : toEquip) {
        spdlog::info("[InvManager] Equip armor slot {}", s);
        container->equipArmor(s);
        mLastAction = NOW;
        if (!isInstant) break;
    }
}

void InvManager::onPacketInEvent(PacketInEvent& event)
{
    if (event.mPacket->getId() == PacketID::ContainerOpen)
    {
        auto packet = event.getPacket<ContainerOpenPacket>();
        if (mManagementMode.mValue == ManagementMode::ContainerOnly || mManagementMode.mValue == ManagementMode::InvOnly && packet->mType == ContainerType::Inventory)
        {
            mHasOpenContainer = true;
        }
    }
    if (event.mPacket->getId() == PacketID::ContainerClose)
    {
        mHasOpenContainer = false;
    }
}

void InvManager::onPacketOutEvent(PacketOutEvent& event)
{
    if (event.mPacket->getId() == PacketID::ContainerClose)
    {
        mHasOpenContainer = false;
    }
    else if (event.mPacket->getId() == PacketID::ContainerOpen)
    {
        auto packet = event.getPacket<ContainerOpenPacket>();
        if (mManagementMode.mValue == ManagementMode::ContainerOnly || mManagementMode.mValue == ManagementMode::InvOnly && packet->mType == ContainerType::Inventory)
        {
            mHasOpenContainer = true;
        }
    }
}

void InvManager::onPingUpdateEvent(PingUpdateEvent& event)
{
    mLastPing = event.mPing;
}

bool InvManager::isItemUseless(ItemStack* item, int slot)
{
    if (!item->mItem) return true;
    auto player = ClientInstance::get()->getLocalPlayer();
    SItemType itemType = item->getItem()->getItemType();
    auto itemValue = ItemUtils::getItemValue(item);
    auto Inv_Manager = gFeatureManager->mModuleManager->getModule<InvManager>();

    if (itemType == SItemType::Helmet || itemType == SItemType::Chestplate || itemType == SItemType::Leggings || itemType == SItemType::Boots)
    {
        int equippedItemValue = ItemUtils::getItemValue(player->getArmorContainer()->getItem(static_cast<int>(itemType)));
        bool hasFireProtection = item->getEnchantValue(Enchant::FIRE_PROTECTION) > 0;

        if (Inv_Manager->mStealFireProtection.mValue && hasFireProtection) {
            return false;
        }

        return equippedItemValue >= itemValue;
    }

    if (itemType == SItemType::Sword || itemType == SItemType::Pickaxe || itemType == SItemType::Axe || itemType == SItemType::Shovel)
    {
        int bestSlot = ItemUtils::getBestItem(itemType);
        int bestValue = ItemUtils::getItemValue(player->getSupplies()->getContainer()->getItem(bestSlot));

        return bestValue >= itemValue && bestSlot != slot;
    }

    return false;
}