/**
 * Fruit Playground Co-op Multiplayer Prototype Script for Frida
 * Target: Android ARM64
 *
 * Usage:
 *   frida -U -f com.fruitplayground.game -l coop_fp.js
 */

const RVAs = {
    SpawnManager_Spawn:       0x288DB28,
    SpawnManager_Select:      0x288DA8C,
    ActionManager_AddObject:  0x2871920,
    ActionManager_ClearWorld: 0x2870D54,
    DragTool_StartDrag:       0x28C8194,
    DragTool_MoveDragger:     0x28C8C40,
    DragTool_EndDrag:         0x28C86B0,
    LivingPart_FixedUpdate:   0x2868484,
    LivingPart_BreakPart:     0x2869030,
    LivingBehaviour_Damage:   0x2864DEC,
    SliceController_SliceAll: 0x288A6C0,
    GameInput_get_WorldPos:   0x2965C68,
};

let il2cppBase = null;
let isHost = true;
let nextNetId = 1;
const objectToNetId = new Map();
const netIdToObject = new Map();

function waitForModule(moduleName, callback) {
    const interval = setInterval(() => {
        const mod = Process.findModuleByName(moduleName);
        if (mod) {
            clearInterval(interval);
            callback(mod.base);
        }
    }, 100);
}

waitForModule("libil2cpp.so", (base) => {
    il2cppBase = base;
    console.log(`[*] libil2cpp.so loaded at: ${il2cppBase}`);

    installHooks();
});

function getPtr(rva) {
    return il2cppBase.add(rva);
}

function installHooks() {
    console.log("[*] Installing Fruit Playground multiplayer hooks...");

    // 1. Hook SpawnManager.Spawn
    Interceptor.attach(getPtr(RVAs.SpawnManager_Spawn), {
        onEnter(args) {
            const thisPtr = args[0];
            console.log("[Hook] SpawnManager.Spawn called");

            if (!isHost) {
                // Client side: inspect selected item
                const selectedObj = thisPtr.add(0x28).readPointer();
                let itemId = 0;
                if (!selectedObj.isNull()) {
                    itemId = selectedObj.add(0x48).readS32();
                }

                sendWs({
                    type: "SPAWN_REQUEST",
                    itemId: itemId
                });
                console.log(`[Client] Intercepted Spawn, sent request for itemId=${itemId}`);
            }
        }
    });

    // 2. Hook ActionManager.AddObject (assign NetID on Host)
    Interceptor.attach(getPtr(RVAs.ActionManager_AddObject), {
        onEnter(args) {
            const gameObjPtr = args[0];
            if (isHost && !gameObjPtr.isNull()) {
                const netId = nextNetId++;
                const key = gameObjPtr.toString();
                objectToNetId.set(key, netId);
                netIdToObject.set(netId, gameObjPtr);

                sendWs({
                    type: "SPAWN_NOTIFY",
                    netId: netId
                });
                console.log(`[Host] Object ${key} assigned NetId=${netId}`);
            }
        }
    });

    // 3. Hook DragTool (StartDrag, MoveDragger, EndDrag)
    Interceptor.attach(getPtr(RVAs.DragTool_StartDrag), {
        onEnter(args) {
            const thisPtr = args[0];
            const worldX = thisPtr.add(0x88).readFloat();
            const worldY = thisPtr.add(0x8C).readFloat();

            sendWs({
                type: "DRAG_EVENT",
                action: "START",
                x: worldX,
                y: worldY
            });
        }
    });

    Interceptor.attach(getPtr(RVAs.DragTool_MoveDragger), {
        onEnter(args) {
            const thisPtr = args[0];
            const isDrag = thisPtr.add(0x78).readU8() !== 0;
            if (isDrag) {
                const worldX = thisPtr.add(0x88).readFloat();
                const worldY = thisPtr.add(0x8C).readFloat();

                sendWs({
                    type: "DRAG_EVENT",
                    action: "MOVE",
                    x: worldX,
                    y: worldY
                });
            }
        }
    });

    Interceptor.attach(getPtr(RVAs.DragTool_EndDrag), {
        onEnter(args) {
            sendWs({
                type: "DRAG_EVENT",
                action: "END"
            });
        }
    });

    // 4. Hook LivingPart.FixedUpdate (Physics Streaming on Host)
    let physicsTick = 0;
    Interceptor.attach(getPtr(RVAs.LivingPart_FixedUpdate), {
        onEnter(args) {
            if (!isHost) return;

            const partPtr = args[0];
            if (++physicsTick % 20 === 0) { // Stream sample
                const key = partPtr.toString();
                const netId = objectToNetId.get(key) || 0;
                sendWs({
                    type: "PHYSICS_SYNC",
                    netId: netId
                });
            }
        }
    });

    // 5. Hook SliceController.SliceAll
    Interceptor.attach(getPtr(RVAs.SliceController_SliceAll), {
        onEnter(args) {
            // Vector2 a passed by value in registers (ARM64: x1/s0-s1 or stack)
            console.log("[Hook] SliceController.SliceAll triggered");
            sendWs({
                type: "SLICE_EVENT"
            });
        }
    });

    console.log("[+] All hooks initialized successfully!");
}

// WebSocket or IPC bridge simulation
function sendWs(msgObj) {
    send(JSON.stringify(msgObj));
}

recv("set_role", (message) => {
    isHost = (message.role === "host");
    console.log(`[Config] Role switched to: ${isHost ? "HOST" : "CLIENT"}`);
});
