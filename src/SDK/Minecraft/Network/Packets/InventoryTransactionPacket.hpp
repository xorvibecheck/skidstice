#pragma once
#include <SDK/Minecraft/Network/Packets/Packet.hpp>
#include <SDK/Minecraft/Inventory/NetworkItemStackDescriptor.hpp>
#include <SDK/Minecraft/Inventory/ItemStack.hpp>
#include <SDK/Minecraft/Inventory/CompoundTag.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstring>
#include <cstddef>

#pragma pack(push, 8)

enum class InventorySourceType : int {
    InvalidInventory = -1,
    ContainerInventory = 0,
    GlobalInventory = 1,
    WorldInteraction = 2,
    CreativeInventory = 3,
    NonImplementedFeatureTODO = 99999
};

class InventorySource {
public:
    enum class InventorySourceFlags : unsigned int {
        NoFlag = 0x0,
        WorldInteraction_Random = 0x1
    };

    InventorySourceType mType = InventorySourceType::InvalidInventory;
    char mContainerId = -1;
    InventorySourceFlags mFlags = InventorySourceFlags::NoFlag;

    inline bool operator==(InventorySource const& rhs) const {
        return mType == rhs.mType && mContainerId == rhs.mContainerId && mFlags == rhs.mFlags;
    }
    inline bool operator!=(InventorySource const& rhs) const { return !(*this == rhs); }
    inline bool operator<(InventorySource const& rhs) const {
        if (mType != rhs.mType) return mType < rhs.mType;
        if (mContainerId != rhs.mContainerId) return mContainerId < rhs.mContainerId;
        return mFlags < rhs.mFlags;
    }
    inline bool operator>(InventorySource const& rhs) const { return rhs < *this; }
};

namespace std {
    template <> struct hash<InventorySource> {
        size_t operator()(InventorySource const& key) const {
            return static_cast<size_t>(key.mContainerId)
                 ^ (static_cast<size_t>(static_cast<unsigned int>(key.mType)) << 16);
        }
    };
}

class InventoryAction {
private:
    static constexpr std::size_t kSize        = 0x200;
    static constexpr std::size_t kOff_Source  = 0x00;
    static constexpr std::size_t kOff_Slot    = 0x0C;
    static constexpr std::size_t kOff_FromDesc= 0x10;
    static constexpr std::size_t kOff_ToDesc  = 0x70;
    static constexpr std::size_t kOff_FromItem= 0xD0;
    static constexpr std::size_t kOff_ToItem  = 0x168;

    alignas(8) std::uint8_t _raw[kSize]{};

public:
    InventoryAction() {
        std::memset(_raw, 0, kSize);
        mSource_ref().mType = InventorySourceType::ContainerInventory;
    }

    InventoryAction(int slot, ItemStack* sourceItem, ItemStack* targetItem) : InventoryAction() {
        Slot_ref() = static_cast<unsigned int>(slot);
        if (sourceItem) {
            std::memcpy(&_raw[kOff_FromItem], sourceItem, std::min<std::size_t>(sizeof(ItemStack), 0x98));
            mFromItemDescriptor_ref() = NetworkItemStackDescriptor(*sourceItem);
        }
        if (targetItem) {
            std::memcpy(&_raw[kOff_ToItem],   targetItem, std::min<std::size_t>(sizeof(ItemStack), 0x98));
            mToItemDescriptor_ref() = NetworkItemStackDescriptor(*targetItem);
        }
    }

    InventorySource&               mSource_ref()             { return *reinterpret_cast<InventorySource*>(&_raw[kOff_Source]); }
    const InventorySource&         mSource_ref()       const { return *reinterpret_cast<const InventorySource*>(&_raw[kOff_Source]); }

    unsigned int&                  Slot_ref()                { return *reinterpret_cast<unsigned int*>(&_raw[kOff_Slot]); }
    const unsigned int&            Slot_ref()          const { return *reinterpret_cast<const unsigned int*>(&_raw[kOff_Slot]); }

    NetworkItemStackDescriptor&    mFromItemDescriptor_ref() { return *reinterpret_cast<NetworkItemStackDescriptor*>(&_raw[kOff_FromDesc]); }
    const NetworkItemStackDescriptor& mFromItemDescriptor_ref() const { return *reinterpret_cast<const NetworkItemStackDescriptor*>(&_raw[kOff_FromDesc]); }

    NetworkItemStackDescriptor&    mToItemDescriptor_ref()   { return *reinterpret_cast<NetworkItemStackDescriptor*>(&_raw[kOff_ToDesc]); }
    const NetworkItemStackDescriptor& mToItemDescriptor_ref() const { return *reinterpret_cast<const NetworkItemStackDescriptor*>(&_raw[kOff_ToDesc]); }

    ItemStack&                     mFromItem_ref()           { return *reinterpret_cast<ItemStack*>(&_raw[kOff_FromItem]); }
    const ItemStack&               mFromItem_ref()     const { return *reinterpret_cast<const ItemStack*>(&_raw[kOff_FromItem]); }

    ItemStack&                     mToItem_ref()             { return *reinterpret_cast<ItemStack*>(&_raw[kOff_ToItem]); }
    const ItemStack&               mToItem_ref()       const { return *reinterpret_cast<const ItemStack*>(&_raw[kOff_ToItem]); }
};

// compat champs → transforme `action.mSource` en `action.mSource_ref()` etc.
#ifndef IA_COMPAT_NO_FIELD_MACROS
#define mSource             mSource_ref()
#define Slot                Slot_ref()
#define mFromItemDescriptor mFromItemDescriptor_ref()
#define mToItemDescriptor   mToItemDescriptor_ref()
#define mFromItem           mFromItem_ref()
#define mToItem             mToItem_ref()
#endif

class InventoryTransactionItemGroup {
public:
    InventoryTransactionItemGroup() = delete;
    int mItemId;
    int mItemAux;
    std::unique_ptr<class CompoundTag> mTag;
    int mCount;
    bool mOverflow;
};

class InventoryTransaction {
public:
    std::unordered_map<InventorySource, std::vector<InventoryAction>> mActions;
    std::vector<InventoryTransactionItemGroup> mItems;

#ifndef INV_TX_ADD_HAS_BOOL
#define INV_TX_ADD_HAS_BOOL 0
#endif
    void addAction(InventoryAction const& action) {
        void* func = reinterpret_cast<void*>(SigManager::InventoryTransaction_addAction);
#if INV_TX_ADD_HAS_BOOL
        using AddAction3 = void(*)(InventoryTransaction*, InventoryAction const&, bool);
        reinterpret_cast<AddAction3>(func)(this, action, false);
#else
        using AddAction2 = void(*)(InventoryTransaction*, InventoryAction const&);
        reinterpret_cast<AddAction2>(func)(this, action);
#endif
    }
};

class ComplexInventoryTransaction {
public:
    enum class Type : int {
        NormalTransaction = 0x0,
        InventoryMismatch = 0x1,
        ItemUseTransaction = 0x2,
        ItemUseOnEntityTransaction = 0x3,
        ItemReleaseTransaction = 0x4
    };

    uintptr_t** vtable;
    Type type;
    InventoryTransaction data;

    ComplexInventoryTransaction() {
        static uintptr_t vtable_addr = SigManager::ComplexInventoryTransaction_vtable;
        vtable = reinterpret_cast<uintptr_t**>(vtable_addr);
        type = Type::NormalTransaction;
        data = InventoryTransaction();
        data.mActions = std::unordered_map<InventorySource, std::vector<InventoryAction>>();
        data.mItems = std::vector<InventoryTransactionItemGroup>();
    }

    Type getTransacType() { return type; }
};

class ItemUseInventoryTransaction : public ComplexInventoryTransaction {
public:
    enum class ActionType : int32_t { Place = 0, Use = 1, Destroy = 2 };
    enum class PredictedResult : int32_t { Failure, Success };
    enum class TriggerType : int8_t { Unknown, PlayerInput, SimulationTick };

    ActionType mActionType{};
    TriggerType mTriggerType = TriggerType::PlayerInput;
    glm::ivec3 mBlockPos{};
    int32_t mTargetBlockRuntimeId{};
    uint32_t mFace{};
    uint32_t mSlot{};
    NetworkItemStackDescriptor mItemInHand{};
    glm::vec3 mPlayerPos{};
    glm::vec3 mClickPos{};
    PredictedResult mPredictedResult = PredictedResult::Success;

    ItemUseInventoryTransaction() {
        static uintptr_t vtable_addr = SigManager::ItemUseInventoryTransaction_vtable;
        vtable = reinterpret_cast<uintptr_t**>(vtable_addr);
        type = Type::ItemUseTransaction;
        data = InventoryTransaction();
        data.mActions = std::unordered_map<InventorySource, std::vector<InventoryAction>>();
        data.mItems = std::vector<InventoryTransactionItemGroup>();
    }
};

class ItemUseOnActorInventoryTransaction : public ComplexInventoryTransaction {
public:
    enum class ActionType : int32_t { Interact = 0, Attack = 1, ItemInteract = 2 };

    uint64_t mActorId{};
    ActionType mActionType{};
    int32_t mSlot{};
    NetworkItemStackDescriptor mItemInHand{};
    glm::vec3 mPlayerPos{};
    glm::vec3 mClickPos{};

    ItemUseOnActorInventoryTransaction() {
        static uintptr_t vtable_addr = SigManager::ItemUseOnActorInventoryTransaction_vtable;
        vtable = reinterpret_cast<uintptr_t**>(vtable_addr);
        type = Type::ItemUseOnEntityTransaction;
        data = InventoryTransaction();
        data.mActions = std::unordered_map<InventorySource, std::vector<InventoryAction>>();
        data.mItems = std::vector<InventoryTransactionItemGroup>();
    }
};

class ItemReleaseInventoryTransaction : public ComplexInventoryTransaction {
public:
    enum class ActionType : int32_t { Release = 0, Use = 1 };

    ActionType mActionType{};
    int32_t mSlot{};
    NetworkItemStackDescriptor mItemInHand{};
    glm::vec3 mHeadPos{};

    ItemReleaseInventoryTransaction() {
        static uintptr_t vtable_addr = SigManager::ItemReleaseInventoryTransaction_vtable;
        vtable = reinterpret_cast<uintptr_t**>(vtable_addr);
        type = Type::ItemReleaseTransaction;
        data = InventoryTransaction();
        data.mActions = std::unordered_map<InventorySource, std::vector<InventoryAction>>();
        data.mItems = std::vector<InventoryTransactionItemGroup>();
    }
};

static_assert(sizeof(ComplexInventoryTransaction) == 104);
static_assert(sizeof(ItemUseOnActorInventoryTransaction) == 240);
static_assert(sizeof(ItemReleaseInventoryTransaction) == 224);

enum class ContainerEnumName : signed char {
    AnvilInputContainer = 0x0,
    AnvilMaterialContainer = 0x1,
    AnvilResultPreviewContainer = 0x2,
    SmithingTableInputContainer = 0x3,
    SmithingTableMaterialContainer = 0x4,
    SmithingTableResultPreviewContainer = 0x5,
    ArmorContainer = 0x6,
    LevelEntityContainer = 0x7,
    BeaconPaymentContainer = 0x8,
    BrewingStandInputContainer = 0x9,
    BrewingStandResultContainer = 0xA,
    BrewingStandFuelContainer = 0xB,
    CombinedHotbarAndInventoryContainer = 0xC,
    CraftingInputContainer = 0xD,
    CraftingOutputPreviewContainer = 0xE,
    RecipeConstructionContainer = 0xF,
    RecipeNatureContainer = 0x10,
    RecipeItemsContainer = 0x11,
    RecipeSearchContainer = 0x12,
    RecipeSearchBarContainer = 0x13,
    RecipeEquipmentContainer = 0x14,
    RecipeBookContainer = 0x15,
    EnchantingInputContainer = 0x16,
    EnchantingMaterialContainer = 0x17,
    FurnaceFuelContainer = 0x18,
    FurnaceIngredientContainer = 0x19,
    FurnaceResultContainer = 0x1A,
    HorseEquipContainer = 0x1B,
    HotbarContainer = 0x1C,
    InventoryContainer = 0x1D,
    ShulkerBoxContainer = 0x1E,
    TradeIngredient1Container = 0x1F,
    TradeIngredient2Container = 0x20,
    TradeResultPreviewContainer = 0x21,
    OffhandContainer = 0x22,
    CompoundCreatorInput = 0x23,
    CompoundCreatorOutputPreview = 0x24,
    ElementConstructorOutputPreview = 0x25,
    MaterialReducerInput = 0x26,
    MaterialReducerOutput = 0x27,
    LabTableInput = 0x28,
    LoomInputContainer = 0x29,
    LoomDyeContainer = 0x2A,
    LoomMaterialContainer = 0x2B,
    LoomResultPreviewContainer = 0x2C,
    BlastFurnaceIngredientContainer = 0x2D,
    SmokerIngredientContainer = 0x2E,
    Trade2Ingredient1Container = 0x2F,
    Trade2Ingredient2Container = 0x30,
    Trade2ResultPreviewContainer = 0x31,
    GrindstoneInputContainer = 0x32,
    GrindstoneAdditionalContainer = 0x33,
    GrindstoneResultPreviewContainer = 0x34,
    StonecutterInputContainer = 0x35,
    StonecutterResultPreviewContainer = 0x36,
    CartographyInputContainer = 0x37,
    CartographyAdditionalContainer = 0x38,
    CartographyResultPreviewContainer = 0x39,
    BarrelContainer = 0x3A,
    CursorContainer = 0x3B,
    CreatedOutputContainer = 0x3C,
    SmithingTableTemplateContainer = 0x3D
};

class InventoryTransactionPacket : public Packet {
public:
    static const PacketID ID = PacketID::InventoryTransaction;

    char pad[0x10];
    std::vector<std::pair<ContainerEnumName, std::vector<unsigned char>>> mLegacySetItemSlots;
    std::unique_ptr<ComplexInventoryTransaction> mTransaction;
    bool mIsClientSide = false;
};

static_assert(sizeof(InventoryTransactionPacket) == 0x68);

#pragma pack(pop)
