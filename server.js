/**
 * Fruit Playground Co-op Relay Server
 * Architecture: Host-Authoritative Room Router (WebSocket & UDP)
 *
 * Requirements:
 *   npm install ws
 * Usage:
 *   node server.js
 */

const WebSocket = require('ws');
const dgram = require('dgram');

const WS_PORT = 8080;
const UDP_PORT = 7777;

// =============================================================================
// Room State
// =============================================================================
class Room {
    constructor(id) {
        this.id = id;
        this.host = null;        // WebSocket or UDP client
        this.clients = new Set(); // Clients
        this.objects = new Map(); // NetId -> Object State
    }

    broadcastFromHost(data, except = null) {
        for (const client of this.clients) {
            if (client !== except && client.readyState === WebSocket.OPEN) {
                client.send(data);
            }
        }
    }

    sendToHost(data) {
        if (this.host && this.host.readyState === WebSocket.OPEN) {
            this.host.send(data);
        }
    }
}

const rooms = new Map();

function getOrCreateRoom(roomId) {
    if (!rooms.has(roomId)) {
        rooms.set(roomId, new Room(roomId));
        console.log(`[Lobby] Room '${roomId}' created.`);
    }
    return rooms.get(roomId);
}

// =============================================================================
// WebSocket Server (Control, Handshake & High-Level Events)
// =============================================================================
const wss = new WebSocket.Server({ port: WS_PORT }, () => {
    console.log(`[WS Server] Listening on ws://0.0.0.0:${WS_PORT}`);
});

wss.on('connection', (ws) => {
    ws.currentRoom = null;
    ws.isHost = false;

    ws.on('message', (message) => {
        let msg;
        try {
            msg = JSON.parse(message);
        } catch (e) {
            return;
        }

        switch (msg.type) {
            case 'JOIN_ROOM': {
                const roomId = msg.roomId || 'default';
                const room = getOrCreateRoom(roomId);
                ws.currentRoom = room;

                if (!room.host) {
                    room.host = ws;
                    ws.isHost = true;
                    ws.send(JSON.stringify({ type: 'ROLE_ASSIGNED', role: 'HOST', roomId }));
                    console.log(`[Lobby] Client assigned as HOST for room '${roomId}'`);
                } else {
                    room.clients.add(ws);
                    ws.isHost = false;
                    ws.send(JSON.stringify({ type: 'ROLE_ASSIGNED', role: 'CLIENT', roomId }));
                    console.log(`[Lobby] Client joined room '${roomId}'`);
                }
                break;
            }

            case 'SPAWN_REQUEST': {
                // Client requests host to spawn an object
                if (ws.currentRoom && !ws.isHost) {
                    ws.currentRoom.sendToHost(message);
                }
                break;
            }

            case 'SPAWN_NOTIFY': {
                // Host confirms spawn with assigned NetId
                if (ws.currentRoom && ws.isHost) {
                    ws.currentRoom.broadcastFromHost(message, ws);
                }
                break;
            }

            case 'DRAG_EVENT':
            case 'CURSOR_MOVE': {
                if (ws.currentRoom) {
                    if (ws.isHost) {
                        ws.currentRoom.broadcastFromHost(message, ws);
                    } else {
                        ws.currentRoom.sendToHost(message);
                    }
                }
                break;
            }

            case 'CLEAR_WORLD': {
                if (ws.currentRoom && ws.isHost) {
                    ws.currentRoom.broadcastFromHost(message, ws);
                }
                break;
            }

            default:
                break;
        }
    });

    ws.on('close', () => {
        if (ws.currentRoom) {
            const room = ws.currentRoom;
            if (ws.isHost) {
                console.log(`[Lobby] Host left room '${room.id}'. Resetting room.`);
                room.host = null;
                room.broadcastFromHost(JSON.stringify({ type: 'HOST_DISCONNECTED' }));
            } else {
                room.clients.delete(ws);
            }
        }
    });
});

// =============================================================================
// UDP Relay Server (High-frequency Physics Synchronization)
// =============================================================================
const udpServer = dgram.createSocket('udp4');
const udpPeers = new Map(); // address:port -> info

udpServer.on('message', (msg, rinfo) => {
    // Fast relay of binary physics packets
    const senderKey = `${rinfo.address}:${rinfo.port}`;
    udpPeers.set(senderKey, rinfo);

    // Broadcast packet to all other connected peers
    for (const [key, peer] of udpPeers.entries()) {
        if (key !== senderKey) {
            udpServer.send(msg, peer.port, peer.address);
        }
    }
});

udpServer.bind(UDP_PORT, () => {
    console.log(`[UDP Relay] Listening on 0.0.0.0:${UDP_PORT}`);
});
