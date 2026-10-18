/**
 * Fruit Playground Co-op Multiplayer Mod with In-Game & JNI GUI
 * Target: Android ARM64 (Unity IL2CPP 2022.x+ / libil2cpp.so)
 * Package: com.onristudio.justplayground
 * Architecture: Host-Authoritative Relay Netplay
 * Features:
 *   - Native Android JNI GUI with Thread Looper & Auto-reconnect
 *   - Auto-show at launch & 3-finger touch trigger
 *   - Dynamic socket re-initialization without restarting game
 *   - Auto-save to /sdcard/Android/data/com.onristudio.justplayground/coop_config.txt
 *   - Embedded Zero-Dependency AArch64 Inline Hook Engine
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
#include <sys/mman.h>
#include <sys/stat.h>

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
// Standalone ARM64 Inline Hook Engine (Zero external dependencies)
// =============================================================================
extern "C" int DobbyHook(void *function_address, void *new_func, void **old_func) {
    if (!function_address || !new_func) return -1;

    uintptr_t target = reinterpret_cast<uintptr_t>(function_address);
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;
    uintptr_t page_start = target & ~(page_size - 1);

    // 1. Create trampoline to call original function
    if (old_func) {
        void *trampoline = mmap(nullptr, 64, PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (trampoline != MAP_FAILED) {
            uint8_t *t_ptr = reinterpret_cast<uint8_t*>(trampoline);

            // Copy first 16 bytes (4 ARM64 instructions)
            memcpy(t_ptr, reinterpret_cast<void*>(target), 16);

            // Append jump back to (target + 16):
            // ldr x17, #8   (0x58000051)
            // br  x17       (0xd61f0220)
            // .quad target + 16
            uint32_t *jump_back = reinterpret_cast<uint32_t*>(t_ptr + 16);
            jump_back[0] = 0x58000051;
            jump_back[1] = 0xd61f0220;
            *reinterpret_cast<uint64_t*>(t_ptr + 24) = target + 16;

            __builtin___clear_cache(reinterpret_cast<char*>(t_ptr), reinterpret_cast<char*>(t_ptr + 32));
            *old_func = trampoline;
        }
    }

    // 2. Unprotect target page
    if (mprotect(reinterpret_cast<void*>(page_start), page_size * 2, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return -1;
    }

    // 3. Write 16-byte absolute detour:
    // ldr x17, #8   (0x58000051)
    // br  x17       (0xd61f0220)
    // .quad new_func
    uint32_t *target_insn = reinterpret_cast<uint32_t*>(target);
    target_insn[0] = 0x58000051;
    target_insn[1] = 0xd61f0220;
    *reinterpret_cast<uint64_t*>(target + 8) = reinterpret_cast<uint64_t>(new_func);

    // 4. Flush I-cache and re-protect
    __builtin___clear_cache(reinterpret_cast<char*>(target), reinterpret_cast<char*>(target + 16));
    mprotect(reinterpret_cast<void*>(page_start), page_size * 2, PROT_READ | PROT_EXEC);

    return 0;
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
    constexpr uintptr_t DragTool_Update          = 0x28C7760;
    constexpr uintptr_t LivingPart_FixedUpdate   = 0x2868484;
    constexpr uintptr_t LivingPart_BreakPart     = 0x2869030;
    constexpr uintptr_t LivingBehaviour_Damage   = 0x2864DEC;
    constexpr uintptr_t LivingBehaviour_Kill     = 0x28651A8;
    constexpr uintptr_t SliceController_SliceAll = 0x288A6C0;
    constexpr uintptr_t GameInput_get_WorldPos   = 0x2965C68;
    constexpr uintptr_t UnityEngine_Input_touchCount = 0x5483A78;
}

// =============================================================================
// Unity & Game Struct Representations
// =============================================================================
struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Quaternion { float x, y, z, w; };

struct Il2CppObject {
    void *klass;
    void *monitor;
};

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

struct SpawnManager_o : Il2CppObject {
    uint8_t pad_mono[0x20 - sizeof(Il2CppObject)];
    bool isFlipped;          // 0x20
    bool isAdReady;          // 0x21
    uint8_t pad_22[6];
    ItemData_o *selectedObject; // 0x28
};

struct DragTool_o : Il2CppObject {
    uint8_t pad_mono[0x60 - sizeof(Il2CppObject)];
    void *draggedPhysicalBehaviour; // 0x60
    void *joint;             // 0x68
    void *rb;                // 0x70
    bool isDrag;             // 0x78
    bool isRotate;           // 0x79
    bool canDrag;            // 0x7A
    bool canRotate;          // 0x7B
    uint8_t pad_7C[0x88 - 0x7C];
    Vector2 worldPosition;   // 0x88
    Vector2 screenPosition;  // 0x90
};

struct LivingPart_o : Il2CppObject {
    uint8_t pad_mono[0x20 - sizeof(Il2CppObject)];
    void *humanBeh;          // 0x20
    uint8_t pad_28[0xE8 - 0x28];
    void *rb;                // 0xE8
    void *po;                // 0xF0
    void *_transform;        // 0xF8
};

struct LivingBehaviour_o : Il2CppObject {
    uint8_t pad_mono[0x20 - sizeof(Il2CppObject)];
    float maxHealth;         // 0x20
    float health;            // 0x24
};

struct SliceController_o : Il2CppObject {};

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
#pragma pack(pop)

// =============================================================================
// Global State & Configuration Persistence
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

static char g_server_ip[64] = "192.168.0.1";
static int  g_server_port   = 7777;

static JavaVM *g_jvm = nullptr;
static jclass g_cached_unity_class = nullptr;
static bool g_dialog_open = false;
static int g_three_finger_cooldown = 0;

static const char* CONFIG_PRIMARY  = "/sdcard/Android/data/com.onristudio.justplayground/coop_config.txt";
static const char* CONFIG_FALLBACK = "/data/data/com.onristudio.justplayground/files/coop_config.txt";

static void load_config() {
    FILE *fp = fopen(CONFIG_PRIMARY, "r");
    if (!fp) fp = fopen(CONFIG_FALLBACK, "r");
    if (fp) {
        char line[128] = {0};
        if (fgets(line, sizeof(line), fp)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            char *nl = strchr(p, '\n');
            if (nl) *nl = '\0';
            nl = strchr(p, '\r');
            if (nl) *nl = '\0';

            char *colon = strchr(p, ':');
            if (colon) {
                *colon = '\0';
                strncpy(g_server_ip, p, sizeof(g_server_ip) - 1);
                int p_val = atoi(colon + 1);
                if (p_val > 0 && p_val < 65536) g_server_port = p_val;
            } else if (strlen(p) > 0) {
                strncpy(g_server_ip, p, sizeof(g_server_ip) - 1);
            }
        }
        fclose(fp);
        LOGI("Loaded config from file: %s:%d", g_server_ip, g_server_port);
    } else {
        LOGI("No config file found. Using default: %s:%d", g_server_ip, g_server_port);
    }
}

static void save_config(const char* ip, int port) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s:%d\n", ip, port);

    mkdir("/sdcard/Android/data/com.onristudio.justplayground", 0777);
    FILE *fp = fopen(CONFIG_PRIMARY, "w");
    if (fp) {
        fputs(buf, fp);
        fclose(fp);
        LOGI("Saved config to %s", CONFIG_PRIMARY);
    }

    FILE *fp2 = fopen(CONFIG_FALLBACK, "w");
    if (fp2) {
        fputs(buf, fp2);
        fclose(fp2);
        LOGI("Saved config to %s", CONFIG_FALLBACK);
    }
}

// Re-initializes socket connection dynamically with new IP/port
static void reconnect_socket(const char* new_ip, int new_port) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    LOGI("[Net] Reconnecting socket to %s:%d...", new_ip, new_port);

    if (g_sock_fd >= 0) {
        close(g_sock_fd);
        g_sock_fd = -1;
    }

    strncpy(g_server_ip, new_ip, sizeof(g_server_ip) - 1);
    g_server_port = new_port;

    g_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock_fd >= 0) {
        memset(&g_server_addr, 0, sizeof(g_server_addr));
        g_server_addr.sin_family = AF_INET;
        g_server_addr.sin_port = htons(g_server_port);
        inet_pton(AF_INET, g_server_ip, &g_server_addr.sin_addr);

        PktHandshake hs{};
        hs.isHost = g_is_host ? 1 : 0;
        strncpy(hs.roomId, "default", sizeof(hs.roomId) - 1);

        std::vector<uint8_t> buffer(sizeof(NetHeader) + sizeof(hs));
        NetHeader *hdr = reinterpret_cast<NetHeader*>(buffer.data());
        hdr->magic = 0x4650434F;
        hdr->type = PKT_HANDSHAKE;
        hdr->length = sizeof(hs);
        memcpy(buffer.data() + sizeof(NetHeader), &hs, sizeof(hs));

        sendto(g_sock_fd, buffer.data(), buffer.size(), 0,
               (struct sockaddr*)&g_server_addr, sizeof(g_server_addr));

        save_config(g_server_ip, g_server_port);
        LOGI("[Net] Reconnected and sent Handshake to %s:%d", g_server_ip, g_server_port);
    } else {
        LOGE("[Net] Failed to create socket during reconnect");
    }
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
// JNI Native Android ShowInputDialog Implementation with Looper
// =============================================================================
static JNIEnv* get_jni_env() {
    if (!g_jvm) {
        void *art = dlopen("libart.so", RTLD_NOW);
        if (art) {
            typedef jint (*JNI_GetCreatedJavaVMs_t)(JavaVM**, jsize, jsize*);
            auto get_vms = (JNI_GetCreatedJavaVMs_t)dlsym(art, "JNI_GetCreatedJavaVMs");
            if (get_vms) {
                JavaVM *vms[1];
                jsize num = 0;
                if (get_vms(vms, 1, &num) == JNI_OK && num > 0) {
                    g_jvm = vms[0];
                }
            }
        }
    }
    if (!g_jvm) return nullptr;
    JNIEnv *env = nullptr;
    jint res = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        g_jvm->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

static jobject get_unity_activity(JNIEnv *env) {
    if (!env) return nullptr;
    jclass unity_player = g_cached_unity_class;
    if (!unity_player) {
        unity_player = env->FindClass("com/unity3d/player/UnityPlayer");
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (!unity_player) return nullptr;

    jfieldID act_field = env->GetStaticFieldID(unity_player, "currentActivity", "Landroid/app/Activity;");
    if (!act_field) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }
    return env->GetStaticObjectField(unity_player, act_field);
}

void ShowInputDialog() {
    if (g_dialog_open) return;
    g_dialog_open = true;

    pthread_t dlg_tid;
    pthread_create(&dlg_tid, nullptr, [](void*) -> void* {
        JNIEnv *env = get_jni_env();
        if (!env) { g_dialog_open = false; return nullptr; }

        // 1. Prepare Looper on this thread so Handler and Views can be created safely
        jclass looperClass = env->FindClass("android/os/Looper");
        if (looperClass) {
            jmethodID myLooperMethod = env->GetStaticMethodID(looperClass, "myLooper", "()Landroid/os/Looper;");
            jobject myLooper = env->CallStaticObjectMethod(looperClass, myLooperMethod);
            if (!myLooper) {
                jmethodID prepareMethod = env->GetStaticMethodID(looperClass, "prepare", "()V");
                env->CallStaticVoidMethod(looperClass, prepareMethod);
            }
        }

        jobject activity = get_unity_activity(env);
        if (!activity) {
            LOGE("Cannot show dialog: Unity currentActivity is null");
            g_dialog_open = false;
            return nullptr;
        }

        char current_str[128];
        snprintf(current_str, sizeof(current_str), "%s:%d", g_server_ip, g_server_port);

        // 2. AlertDialog.Builder
        jclass builderClass = env->FindClass("android/app/AlertDialog$Builder");
        if (!builderClass) { g_dialog_open = false; return nullptr; }
        jmethodID builderCtor = env->GetMethodID(builderClass, "<init>", "(Landroid/content/Context;)V");
        jobject builder = env->NewObject(builderClass, builderCtor, activity);

        jmethodID setTitle = env->GetMethodID(builderClass, "setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;");
        jstring titleStr = env->NewStringUTF("Fruit Playground Co-op Server");
        env->CallObjectMethod(builder, setTitle, titleStr);

        jmethodID setMessage = env->GetMethodID(builderClass, "setMessage", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;");
        jstring msgStr = env->NewStringUTF("Введите адрес сервера (IP:Порт):");
        env->CallObjectMethod(builder, setMessage, msgStr);

        // 3. Layout with EditText
        jclass layoutClass = env->FindClass("android/widget/LinearLayout");
        jmethodID layoutCtor = env->GetMethodID(layoutClass, "<init>", "(Landroid/content/Context;)V");
        jobject layout = env->NewObject(layoutClass, layoutCtor, activity);
        jmethodID setOrientation = env->GetMethodID(layoutClass, "setOrientation", "(I)V");
        env->CallVoidMethod(layout, setOrientation, 1);
        jmethodID setPadding = env->GetMethodID(layoutClass, "setPadding", "(IIII)V");
        env->CallVoidMethod(layout, setPadding, 50, 20, 50, 20);

        jclass editTextClass = env->FindClass("android/widget/EditText");
        jmethodID editTextCtor = env->GetMethodID(editTextClass, "<init>", "(Landroid/content/Context;)V");
        jobject editText = env->NewObject(editTextClass, editTextCtor, activity);

        jmethodID setHint = env->GetMethodID(editTextClass, "setHint", "(Ljava/lang/CharSequence;)V");
        jstring hintStr = env->NewStringUTF("192.168.0.1:7777");
        env->CallVoidMethod(editText, setHint, hintStr);

        jmethodID setText = env->GetMethodID(editTextClass, "setText", "(Ljava/lang/CharSequence;)V");
        jstring initText = env->NewStringUTF(current_str);
        env->CallVoidMethod(editText, setText, initText);

        jmethodID addView = env->GetMethodID(layoutClass, "addView", "(Landroid/view/View;)V");
        env->CallVoidMethod(layout, addView, editText);

        jmethodID setView = env->GetMethodID(builderClass, "setView", "(Landroid/view/View;)Landroid/app/AlertDialog$Builder;");
        env->CallObjectMethod(builder, setView, layout);

        jmethodID setPosBtn = env->GetMethodID(builderClass, "setPositiveButton",
            "(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;");
        jstring btnApply = env->NewStringUTF("Применить и подключиться");
        env->CallObjectMethod(builder, setPosBtn, btnApply, nullptr);

        jmethodID setNegBtn = env->GetMethodID(builderClass, "setNegativeButton",
            "(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;");
        jstring btnCancel = env->NewStringUTF("Отмена");
        env->CallObjectMethod(builder, setNegBtn, btnCancel, nullptr);

        // 4. Create & Show Dialog
        jmethodID createDialog = env->GetMethodID(builderClass, "create", "()Landroid/app/AlertDialog;");
        jobject dialog = env->CallObjectMethod(builder, createDialog);

        jclass dialogClass = env->FindClass("android/app/AlertDialog");
        jmethodID showDialog = env->GetMethodID(dialogClass, "show", "()V");
        env->CallVoidMethod(dialog, showDialog);

        jobject globalDialog = env->NewGlobalRef(dialog);
        jobject globalEditText = env->NewGlobalRef(editText);

        jmethodID isShowing = env->GetMethodID(dialogClass, "isShowing", "()Z");
        jmethodID getText = env->GetMethodID(editTextClass, "getText", "()Landroid/text/Editable;");
        jclass charSeqClass = env->FindClass("java/lang/CharSequence");
        jmethodID toStr = env->GetMethodID(charSeqClass, "toString", "()Ljava/lang/String;");

        // 5. Poll until dismissed
        while (env->CallBooleanMethod(globalDialog, isShowing)) {
            usleep(150000);
        }

        jobject editable = env->CallObjectMethod(globalEditText, getText);
        if (editable) {
            jstring strObj = (jstring)env->CallObjectMethod(editable, toStr);
            if (strObj) {
                const char *utf = env->GetStringUTFChars(strObj, nullptr);
                if (utf && strlen(utf) > 0) {
                    char new_ip[64] = {0};
                    int new_port = 7777;
                    char temp[128];
                    strncpy(temp, utf, sizeof(temp) - 1);
                    char *colon = strchr(temp, ':');
                    if (colon) {
                        *colon = '\0';
                        strncpy(new_ip, temp, sizeof(new_ip) - 1);
                        new_port = atoi(colon + 1);
                        if (new_port <= 0 || new_port > 65535) new_port = 7777;
                    } else {
                        strncpy(new_ip, temp, sizeof(new_ip) - 1);
                    }
                    reconnect_socket(new_ip, new_port);
                }
                env->ReleaseStringUTFChars(strObj, utf);
            }
        }

        env->DeleteGlobalRef(globalDialog);
        env->DeleteGlobalRef(globalEditText);
        g_dialog_open = false;
        return nullptr;
    }, nullptr);
    pthread_detach(dlg_tid);
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
static void (*orig_DragTool_Update)(DragTool_o*, void*) = nullptr;
static void (*orig_LivingPart_FixedUpdate)(LivingPart_o*, void*) = nullptr;
static void (*orig_SliceController_SliceAll)(SliceController_o*, Vector2, Vector2, void*) = nullptr;
static Vector3 (*orig_GameInput_get_WorldPos)(void*) = nullptr;
static int (*orig_get_touchCount)() = nullptr;

// =============================================================================
// Hook Implementations
// =============================================================================

// Three-finger touch detection in DragTool_Update
static void hook_DragTool_Update(DragTool_o *tool, void *method) {
    orig_DragTool_Update(tool, method);

    if (g_three_finger_cooldown > 0) {
        g_three_finger_cooldown--;
    } else {
        if (!orig_get_touchCount && g_il2cpp_base) {
            orig_get_touchCount = (int(*)())(g_il2cpp_base + Offsets::UnityEngine_Input_touchCount);
        }
        if (orig_get_touchCount) {
            int touches = orig_get_touchCount();
            if (touches >= 3 && !g_dialog_open) {
                g_three_finger_cooldown = 90; // Cooldown 1.5s
                LOGI("Three-finger touch detected! Opening Server IP Dialog...");
                ShowInputDialog();
            }
        }
    }
}

// Hook on Object Spawning
static void hook_SpawnManager_Spawn(SpawnManager_o *mgr, void *method) {
    if (!mgr) return;

    if (!g_is_host) {
        if (mgr->selectedObject) {
            PktSpawn req{};
            req.netId = 0;
            req.itemId = mgr->selectedObject->itemId;
            Vector3 worldPos = orig_GameInput_get_WorldPos(nullptr);
            req.posX = worldPos.x;
            req.posY = worldPos.y;
            req.rotZ = 0.0f;
            send_packet(PKT_SPAWN, &req, sizeof(req));
            LOGI("[Client] Requested spawn item ID: %d at (%.2f, %.2f)", req.itemId, req.posX, req.posY);
        }
        return;
    }
    orig_SpawnManager_Spawn(mgr, method);
}

// Intercept Object Registration to Assign Network ID
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
        pkt.itemId = 0;
        send_packet(PKT_SPAWN, &pkt, sizeof(pkt));
    }
}

// Clear World Hook
static void hook_ActionManager_ClearWorld(void *mgr, bool resetContraption, void *method) {
    orig_ActionManager_ClearWorld(mgr, resetContraption, method);
    if (g_is_host) {
        send_packet(PKT_CLEAR_WORLD, nullptr, 0);
    }
}

// Physics Tick Hook
static void hook_LivingPart_FixedUpdate(LivingPart_o *part, void *method) {
    orig_LivingPart_FixedUpdate(part, method);

    if (!part) return;

    if (g_is_host) {
        static int tick = 0;
        if (++tick % 2 == 0) {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            auto it = g_object_to_net.find(part);
            if (it != g_object_to_net.end()) {
                PhysicsObjectSync sync{};
                sync.netId = it->second;
                send_packet(PKT_PHYSICS_SYNC, &sync, sizeof(sync));
            }
        }
    }
}

// Drag Tool Hooks
static void hook_DragTool_StartDrag(DragTool_o *tool, void *method) {
    orig_DragTool_StartDrag(tool, method);
    if (!g_is_host && tool) {
        PktDrag dragPkt{0, 0, tool->worldPosition.x, tool->worldPosition.y};
        send_packet(PKT_DRAG, &dragPkt, sizeof(dragPkt));
    }
}

static void hook_DragTool_MoveDragger(DragTool_o *tool, bool touch, void *method) {
    orig_DragTool_MoveDragger(tool, touch, method);
    if (!g_is_host && tool && tool->isDrag) {
        PktDrag dragPkt{0, 1, tool->worldPosition.x, tool->worldPosition.y};
        send_packet(PKT_DRAG, &dragPkt, sizeof(dragPkt));
    }
}

static void hook_DragTool_EndDrag(DragTool_o *tool, void *method) {
    orig_DragTool_EndDrag(tool, method);
    if (!g_is_host && tool) {
        PktDrag dragPkt{0, 2, 0, 0};
        send_packet(PKT_DRAG, &dragPkt, sizeof(dragPkt));
    }
}

// Slice Hook
static void hook_SliceController_SliceAll(SliceController_o *ctrl, Vector2 a, Vector2 b, void *method) {
    orig_SliceController_SliceAll(ctrl, a, b, method);
    PktSlice pkt{a.x, a.y, b.x, b.y};
    send_packet(PKT_SLICE, &pkt, sizeof(pkt));
}

// Input Hook (Cursor streaming)
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
    while (true) {
        if (g_sock_fd < 0) {
            usleep(100000);
            continue;
        }
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
                }
                break;
            }
            case PKT_SPAWN: {
                if (hdr->length >= sizeof(PktSpawn)) {
                    const PktSpawn *s = reinterpret_cast<const PktSpawn*>(payload);
                    LOGI("[Net] Spawn packet: NetId=%u, ItemId=%d at (%.2f, %.2f)",
                         s->netId, s->itemId, s->posX, s->posY);
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

// =============================================================================
// Library Entrypoint & Hook Initialization
// =============================================================================
JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    g_jvm = vm;
    JNIEnv *env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK && env) {
        jclass up = env->FindClass("com/unity3d/player/UnityPlayer");
        if (up) {
            g_cached_unity_class = (jclass)env->NewGlobalRef(up);
        } else {
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
    }
    LOGI("JNI_OnLoad called, cached JavaVM & UnityPlayer class");
    return JNI_VERSION_1_6;
}

__attribute__((constructor))
void init_coop_mod() {
    LOGI("Fruit Playground Co-op Mod loading...");

    load_config();

    // Background thread to wait for libil2cpp and initialize hooks
    pthread_t init_thread;
    pthread_create(&init_thread, nullptr, [](void*) -> void* {
        while (g_il2cpp_base == 0) {
            g_il2cpp_base = get_module_base("libil2cpp.so");
            if (!g_il2cpp_base) usleep(100000);
        }
        LOGI("libil2cpp.so located at base: 0x%lx", g_il2cpp_base);

        // Initial socket connection
        reconnect_socket(g_server_ip, g_server_port);

        // Start background UDP receiver
        pthread_t net_tid;
        pthread_create(&net_tid, nullptr, net_receive_thread, nullptr);
        pthread_detach(net_tid);

        // Install Hooks
        #define INSTALL_HOOK(target_rva, hook_fn, orig_ptr) \
            DobbyHook((void*)(g_il2cpp_base + target_rva), (void*)hook_fn, (void**)&orig_ptr);

        INSTALL_HOOK(Offsets::SpawnManager_Spawn,       hook_SpawnManager_Spawn,       orig_SpawnManager_Spawn);
        INSTALL_HOOK(Offsets::ActionManager_AddObject,  hook_ActionManager_AddObject,  orig_ActionManager_AddObject);
        INSTALL_HOOK(Offsets::ActionManager_ClearWorld, hook_ActionManager_ClearWorld, orig_ActionManager_ClearWorld);
        INSTALL_HOOK(Offsets::DragTool_StartDrag,       hook_DragTool_StartDrag,       orig_DragTool_StartDrag);
        INSTALL_HOOK(Offsets::DragTool_MoveDragger,     hook_DragTool_MoveDragger,     orig_DragTool_MoveDragger);
        INSTALL_HOOK(Offsets::DragTool_EndDrag,         hook_DragTool_EndDrag,         orig_DragTool_EndDrag);
        INSTALL_HOOK(Offsets::DragTool_Update,          hook_DragTool_Update,          orig_DragTool_Update);
        INSTALL_HOOK(Offsets::LivingPart_FixedUpdate,   hook_LivingPart_FixedUpdate,   orig_LivingPart_FixedUpdate);
        INSTALL_HOOK(Offsets::SliceController_SliceAll, hook_SliceController_SliceAll, orig_SliceController_SliceAll);
        INSTALL_HOOK(Offsets::GameInput_get_WorldPos,   hook_GameInput_get_WorldPos,   orig_GameInput_get_WorldPos);

        #undef INSTALL_HOOK
        LOGI("All Fruit Playground co-op hooks installed successfully!");

        // Auto-show connection dialog at startup after 3 seconds
        sleep(3);
        ShowInputDialog();

        return nullptr;
    }, nullptr);
    pthread_detach(init_thread);
}
