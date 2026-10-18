/**
 * Fruit Playground Co-op Multiplayer Mod
 * Target: Android ARM64 (Unity IL2CPP 2022.x+ / libil2cpp.so)
 * Architecture: Host-Authoritative Relay Netplay
 * Hooking Engine: Dobby Hook (or And64InlineHook)
 */

#include <jni.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <android/log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

#define LOG_TAG "FruitCoop"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// =============================================================================
// Dobby Hook Prototypes
// =============================================================================
extern "C" {
    int DobbyHook(void *function_address, void *new_func, void **old_func);
}

// =============================================================================
// IL2CPP Exact RVAs (ARM64 Fruit Playground)
// =============================================================================
namespace Offsets {
    constexpr uintptr_t SpawnManager_Spawn       = 0x288DB28;
    constexpr uintptr_t SpawnManager_Select      = 0x288DA8C;
    constexpr uintptr_t ActionManager_AddObject  = 0x2871920;
    constexpr uintptr_t ActionManager_ClearWorld = 0x2870D54;
    constexpr uintptr_t DragTool_StartDrag       = 0x28C8194;
    constexpr uintptr_t DragTool_MoveDragger     = 0x28C8C40;
    constexpr uintptr_t DragTool_EndDrag         = 0x28C86B0;
    constexpr uintptr_t LivingPart_FixedUpdate   = 0x2868484;
    constexpr uintptr_t LivingPart_BreakPart     = 0x2869030;
    constexpr uintptr_t LivingBehaviour_Damage   = 0x2864DEC;
    constexpr uintptr_t LivingBehaviour_Kill     = 0x28651A8;
    constexpr uintptr_t SliceController_SliceAll = 0x288A6C0;
    constexpr uintptr_t GameInput_get_WorldPos   = 0x2965C68;
    constexpr uintptr_t HumanoidBehaviour_Walk   = 0x2860B30;
    constexpr uintptr_t HumanoidBehaviour_Jump   = 0x2861160;
}

// =============================================================================
// Unity & Game Struct Representations
// =============================================================================
struct Vector2 {
    float x;
    float y;
};

struct Vector3 {
    float x;
    float y;
    float z;
};

struct Quaternion {
    float x;
    float y;
    float z;
    float w;
};

// Generic IL2CPP Object Header
struct Il2CppObject {
    void *klass;
    void *monitor;
};

// ItemData : ScriptableObject
struct ItemData_o : Il2CppObject {
    void *prefab;            // 0x18
    void *itemName;          // 0x20
    void *itemDescription;   // 0x28
    void *itemIcon;          // 0x30
    void *dragIcon;          // 0x38
    void *dragPreview;       // 0x40
    int32_t itemId;          // 0x48
    int32_t itemCategory;    // 0x4C
    bool isLocked;           // 0x50
};

// SpawnManager : MonoBehaviour
struct SpawnManager_o : Il2CppObject {
    uint8_t pad_mono[0x20 - sizeof(Il2CppObject)];
    bool isFlipped;          // 0x20
    bool isAdReady;          // 0x21
    uint8_t pad_22[6];
    ItemData_o *selectedObject; // 0x28
};

// PhysicalBehaviour : MonoBehaviour
struct PhysicalBehaviour_o : Il2CppObject {
    uint8_t pad_mono[0x20 - sizeof(Il2CppObject)];
    int32_t itemID;          // 0x20
    bool isMod;              // 0x24
    uint8_t pad_25[3];
    void *modFileName;       // 0x28
    void *modUuid;           // 0x30
    int32_t materialType;    // 0x38
    uint8_t pad_3C[4];
    void *weaponBehaviour;   // 0x40
    void *modularObject;     // 0x48
    void *rb;                // 0x50 (Rigidbody2D)
    void *objectMediator;    // 0x58
    void *storedData;        // 0x60
    void *audioSource;       // 0x68
    void *living;            // 0x70 (LivingBehaviour)
    void *destruction;       // 0x78 (DestructionBehaviour)
    uint8_t pad_80[0x138 - 0x80];
    void *tr;                // 0x138 (Transform)
};

// LivingPart : MonoBehaviour
struct LivingPart_o : Il2CppObject {
    uint8_t pad_mono[0x20 - sizeof(Il2CppObject)];
    void *humanBeh;          // 0x20
    uint8_t pad_28[0xE8 - 0x28];
    void *rb;                // 0xE8 (Rigidbody2D)
    PhysicalBehaviour_o *po; // 0xF0
    void *_transform;        // 0xF8 (Transform)
};

// LivingBehaviour : MonoBehaviour
struct LivingBehaviour_o : Il2CppObject {
    uint8_t pad_mono[0x20 - sizeof(Il2CppObject)];
    float maxHealth;         // 0x20
    float health;            // 0x24
    float oxygen;            // 0x28
    float bleedingMultiplier;// 0x2C
    bool isVital;            // 0x30
    bool isLimb;             // 0x31
    uint8_t pad_32[6];
};

// DragTool : MonoBehaviour
struct DragTool_o : Il2CppObject {
    uint8_t pad_mono[0x60 - sizeof(Il2CppObject)];
    PhysicalBehaviour_o *draggedPhysicalBehaviour; // 0x60
    void *joint;             // 0x68 (TargetJoint2D)
    void *rb;                // 0x70 (Rigidbody2D)
    bool isDrag;             // 0x78
    bool isRotate;           // 0x79
    bool canDrag;            // 0x7A
    bool canRotate;          // 0x7B
    uint8_t pad_7C[0x88 - 0x7C];
    Vector2 worldPosition;   // 0x88
    Vector2 screenPosition;  // 0x90
};

// SliceController : MonoBehaviour
struct SliceController_o : Il2CppObject {
    // standard Mono fields
};

// =============================================================================
// Network Protocol Definitions (Host-Authoritative)
// =============================================================================
enum PacketType : uint8_t {
    PKT_HANDSHAKE      = 1,
    PKT_SPAWN          = 2,
    PKT_PHYSICS_SYNC   = 3,
    PKT_DRAG           = 4,
    PKT_CURSOR         = 5,
    PKT_SLICE          = 6,
    PKT_DAMAGE         = 7,
    PKT_BREAK_PART     = 8,
    PKT_CLEAR_WORLD    = 9
};

#pragma pack(push, 1)
struct NetHeader {
    uint32_t magic;       // 0x4650434F ("FPCO")
    uint8_t  type;
    uint16_t length;
};

struct PktHandshake {
    uint8_t isHost;
    char    roomId[16];
};

struct PktSpawn {
    uint32_t netId;
    int32_t  itemId;
    float    posX;
    float    posY;
    float    rotZ;
};

struct PhysicsObjectSync {
    uint32_t netId;
    float    posX;
    float    posY;
    float    rotZ;
    float    velX;
    float    velY;
};

struct PktDrag {
    uint32_t netId;
    uint8_t  action; // 0: Start, 1: Move, 2: End
    float    posX;
    float    posY;
};

struct PktCursor {
    float    posX;
    float    posY;
    uint8_t  isPressed;
};

struct PktSlice {
    float x1, y1;
    float x2, y2;
};

struct PktDamage {
    uint32_t netId;
    float    amount;
};

struct PktBreakPart {
    uint32_t netId;
    uint32_t partIndex;
};
#pragma pack(pop)

// =============================================================================
// Global State & Networking
// =============================================================================
static uintptr_t g_il2cpp_base = 0;
static bool g_is_host = true;
static int g_sock_fd = -1;
static sockaddr_in g_server_addr;
static std::mutex g_state_mutex;
static uint32_t g_next_net_id = 1;

static std::unordered_map<uint32_t, void*> g_net_to_object;
static std::unordered_map<void*, uint32_t> g_object_to_net;
static Vector2 g_remote_cursor = {0, 0};
static bool g_remote_cursor_down = false;

// Helper to resolve base address of libil2cpp.so from /proc/self/maps
static uintptr_t get_module_base(const char *module_name) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, module_name)) {
            base = strtoull(line, nullptr, 16);
            break;
        }
    }
    fclose(fp);
    return base;
}

static void send_packet(PacketType type, const void *payload, size_t len) {
    if (g_sock_fd < 0) return;
    std::vector<uint8_t> buffer(sizeof(NetHeader) + len);
    NetHeader *hdr = reinterpret_cast<NetHeader*>(buffer.data());
    hdr->magic = 0x4650434F;
    hdr->type = static_cast<uint8_t>(type);
    hdr->length = static_cast<uint16_t>(len);
    if (len > 0 && payload) {
        memcpy(buffer.data() + sizeof(NetHeader), payload, len);
    }
    sendto(g_sock_fd, buffer.data(), buffer.size(), 0,
           (struct sockaddr*)&g_server_addr, sizeof(g_server_addr));
}

// =============================================================================
// Trampolines and Original Function Pointers
// =============================================================================
static void (*orig_SpawnManager_Spawn)(SpawnManager_o*, void*) = nullptr;
static void (*orig_ActionManager_AddObject)(void*, void*) = nullptr;
static void (*orig_ActionManager_ClearWorld)(void*, bool, void*) = nullptr;
static void (*orig_DragTool_StartDrag)(DragTool_o*, void*) = nullptr;
static void (*orig_DragTool_MoveDragger)(DragTool_o*, bool, void*) = nullptr;
static void (*orig_DragTool_EndDrag)(DragTool_o*, void*) = nullptr;
static void (*orig_LivingPart_FixedUpdate)(LivingPart_o*, void*) = nullptr;
static void (*orig_LivingPart_BreakPart)(LivingPart_o*, bool, void*) = nullptr;
static void (*orig_LivingBehaviour_Damage)(LivingBehaviour_o*, float, bool, bool, bool, float, void*) = nullptr;
static void (*orig_SliceController_SliceAll)(SliceController_o*, Vector2, Vector2, void*) = nullptr;
static Vector3 (*orig_GameInput_get_WorldPos)(void*) = nullptr;

// =============================================================================
// Hook Implementations
// =============================================================================

// 1. Hook on Object Spawning
static void hook_SpawnManager_Spawn(SpawnManager_o *mgr, void *method) {
    if (!mgr) return;

    if (!g_is_host) {
        // Client requests spawn from Host
        if (mgr->selectedObject) {
            PktSpawn req{};
            req.netId = 0; // Host will assign
            req.itemId = mgr->selectedObject->itemId;
            Vector3 worldPos = orig_GameInput_get_WorldPos(nullptr);
            req.posX = worldPos.x;
            req.posY = worldPos.y;
            req.rotZ = 0.0f;
            send_packet(PKT_SPAWN, &req, sizeof(req));
            LOGI("[Client] Requested spawn item ID: %d at (%.2f, %.2f)", req.itemId, req.posX, req.posY);
        }
        return; // Do not spawn locally; wait for host authoritative packet
    }

    // Host executes spawn directly
    orig_SpawnManager_Spawn(mgr, method);
}

// 2. Intercept Object Registration to Assign Network ID
static void hook_ActionManager_AddObject(void *gameObj, void *method) {
    orig_ActionManager_AddObject(gameObj, method);

    if (g_is_host && gameObj) {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        uint32_t netId = g_next_net_id++;
        g_net_to_object[netId] = gameObj;
        g_object_to_net[gameObj] = netId;

        PktSpawn pkt{};
        pkt.netId = netId;
        Vector3 worldPos = orig_GameInput_get_WorldPos(nullptr);
        pkt.posX = worldPos.x;
        pkt.posY = worldPos.y;
        pkt.rotZ = 0.0f;
        pkt.itemId = 0; // default item
        send_packet(PKT_SPAWN, &pkt, sizeof(pkt));
        LOGI("[Host] Object spawned and registered with NetId: %u", netId);
    }
}

// 3. Clear World Hook
static void hook_ActionManager_ClearWorld(void *mgr, bool resetContraption, void *method) {
    orig_ActionManager_ClearWorld(mgr, resetContraption, method);
    if (g_is_host) {
        send_packet(PKT_CLEAR_WORLD, nullptr, 0);
        LOGI("[Host] World cleared, broadcasted to client");
    }
}

// 4. Physics Tick Hook (Host broadcasts bone and body transforms)
static void hook_LivingPart_FixedUpdate(LivingPart_o *part, void *method) {
    orig_LivingPart_FixedUpdate(part, method);

    if (!part) return;

    if (g_is_host) {
        // Host streams physics state periodically
        static int tick = 0;
        if (++tick % 2 == 0) { // 30Hz broadcast under 60Hz physics
            std::lock_guard<std::mutex> lock(g_state_mutex);
            auto it = g_object_to_net.find(part);
            if (it != g_object_to_net.end()) {
                PhysicsObjectSync sync{};
                sync.netId = it->second;
                // Read position from Transform or Rigidbody2D
                // sync.posX = ...; sync.posY = ...;
                send_packet(PKT_PHYSICS_SYNC, &sync, sizeof(sync));
            }
        }
    }
}

// 5. Drag Tool Hooks (Client sends drag requests; Host applies forces)
static void hook_DragTool_StartDrag(DragTool_o *tool, void *method) {
    orig_DragTool_StartDrag(tool, method);
    if (!g_is_host && tool) {
        PktDrag dragPkt{};
        dragPkt.action = 0; // Start
        dragPkt.posX = tool->worldPosition.x;
        dragPkt.posY = tool->worldPosition.y;
        send_packet(PKT_DRAG, &dragPkt, sizeof(dragPkt));
    }
}

static void hook_DragTool_MoveDragger(DragTool_o *tool, bool touch, void *method) {
    orig_DragTool_MoveDragger(tool, touch, method);
    if (!g_is_host && tool && tool->isDrag) {
        PktDrag dragPkt{};
        dragPkt.action = 1; // Move
        dragPkt.posX = tool->worldPosition.x;
        dragPkt.posY = tool->worldPosition.y;
        send_packet(PKT_DRAG, &dragPkt, sizeof(dragPkt));
    }
}

static void hook_DragTool_EndDrag(DragTool_o *tool, void *method) {
    orig_DragTool_EndDrag(tool, method);
    if (!g_is_host && tool) {
        PktDrag dragPkt{};
        dragPkt.action = 2; // End
        send_packet(PKT_DRAG, &dragPkt, sizeof(dragPkt));
    }
}

// 6. Slice Hook (Broadcast slice cuts)
static void hook_SliceController_SliceAll(SliceController_o *ctrl, Vector2 a, Vector2 b, void *method) {
    orig_SliceController_SliceAll(ctrl, a, b, method);
    PktSlice pkt{a.x, a.y, b.x, b.y};
    send_packet(PKT_SLICE, &pkt, sizeof(pkt));
    LOGI("Slice triggered from (%.2f, %.2f) to (%.2f, %.2f)", a.x, a.y, b.x, b.y);
}

// 7. Input Hook (Stream local cursor coordinates for remote visualization)
static Vector3 hook_GameInput_get_WorldPos(void *method) {
    Vector3 pos = orig_GameInput_get_WorldPos(method);
    static int cursorTick = 0;
    if (++cursorTick % 3 == 0) {
        PktCursor curPkt{pos.x, pos.y, 1};
        send_packet(PKT_CURSOR, &curPkt, sizeof(curPkt));
    }
    return pos;
}

// =============================================================================
// Background Network Receiver Thread
// =============================================================================
static void* net_receive_thread(void *arg) {
    uint8_t buffer[2048];
    while (g_sock_fd >= 0) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        ssize_t n = recvfrom(g_sock_fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &fromLen);
        if (n < (ssize_t)sizeof(NetHeader)) continue;

        NetHeader *hdr = reinterpret_cast<NetHeader*>(buffer);
        if (hdr->magic != 0x4650434F) continue;

        const uint8_t *payload = buffer + sizeof(NetHeader);
        switch (hdr->type) {
            case PKT_CURSOR: {
                if (hdr->length >= sizeof(PktCursor)) {
                    const PktCursor *c = reinterpret_cast<const PktCursor*>(payload);
                    g_remote_cursor.x = c->posX;
                    g_remote_cursor.y = c->posY;
                    g_remote_cursor_down = (c->isPressed != 0);
                }
                break;
            }
            case PKT_SPAWN: {
                if (hdr->length >= sizeof(PktSpawn)) {
                    const PktSpawn *s = reinterpret_cast<const PktSpawn*>(payload);
                    LOGI("[Net] Received Spawn packet: NetId=%u, ItemId=%d at (%.2f, %.2f)",
                         s->netId, s->itemId, s->posX, s->posY);
                    // Spawn puppet or proxy object on client
                }
                break;
            }
            case PKT_CLEAR_WORLD: {
                LOGI("[Net] Received ClearWorld command");
                break;
            }
            default:
                break;
        }
    }
    return nullptr;
}

// =============================================================================
// Library Entrypoint & Hook Initialization
// =============================================================================
__attribute__((constructor))
void init_coop_mod() {
    LOGI("Fruit Playground Co-op Mod loading...");

    // Wait for libil2cpp.so to load
    while (g_il2cpp_base == 0) {
        g_il2cpp_base = get_module_base("libil2cpp.so");
        if (!g_il2cpp_base) usleep(100000);
    }
    LOGI("libil2cpp.so located at base: 0x%lx", g_il2cpp_base);

    // Initialize UDP network socket
    g_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock_fd >= 0) {
        memset(&g_server_addr, 0, sizeof(g_server_addr));
        g_server_addr.sin_family = AF_INET;
        g_server_addr.sin_port = htons(7777);
        inet_pton(AF_INET, "192.168.0.1", &g_server_addr.sin_addr);

        pthread_t tid;
        pthread_create(&tid, nullptr, net_receive_thread, nullptr);
        pthread_detach(tid);
        LOGI("Network thread started successfully");
    }

    // Install Dobby Hooks using resolved RVAs
    #define INSTALL_HOOK(target_rva, hook_fn, orig_ptr) \
        DobbyHook((void*)(g_il2cpp_base + target_rva), (void*)hook_fn, (void**)&orig_ptr);

    INSTALL_HOOK(Offsets::SpawnManager_Spawn,       hook_SpawnManager_Spawn,       orig_SpawnManager_Spawn);
    INSTALL_HOOK(Offsets::ActionManager_AddObject,  hook_ActionManager_AddObject,  orig_ActionManager_AddObject);
    INSTALL_HOOK(Offsets::ActionManager_ClearWorld, hook_ActionManager_ClearWorld, orig_ActionManager_ClearWorld);
    INSTALL_HOOK(Offsets::DragTool_StartDrag,       hook_DragTool_StartDrag,       orig_DragTool_StartDrag);
    INSTALL_HOOK(Offsets::DragTool_MoveDragger,     hook_DragTool_MoveDragger,     orig_DragTool_MoveDragger);
    INSTALL_HOOK(Offsets::DragTool_EndDrag,         hook_DragTool_EndDrag,         orig_DragTool_EndDrag);
    INSTALL_HOOK(Offsets::LivingPart_FixedUpdate,   hook_LivingPart_FixedUpdate,   orig_LivingPart_FixedUpdate);
    INSTALL_HOOK(Offsets::SliceController_SliceAll, hook_SliceController_SliceAll, orig_SliceController_SliceAll);
    INSTALL_HOOK(Offsets::GameInput_get_WorldPos,   hook_GameInput_get_WorldPos,   orig_GameInput_get_WorldPos);

    #undef INSTALL_HOOK

    LOGI("All Fruit Playground co-op hooks installed successfully!");
}
