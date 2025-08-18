import asyncio
import time
import struct
from bleak import BleakScanner, BleakClient
import importlib.metadata

# Show current Bleak version
version = importlib.metadata.version("bleak")
print("Bleak version:", version)

# UUIDs
SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
CHARACTERISTIC_UUID = "87654321-4321-6789-4321-0fedcba98765"

# Decode incoming binary payload (28 bytes)
def decode_binary_payload(data: bytes):
    if len(data) != 32:
        raise ValueError(f"Expected 32 bytes, got {len(data)}")

    # > = big-endian, Q = uint64 (timestamp in ms), 6f = six float32s
    unpacked = struct.unpack(">Q6f", data)
    decoded = {
        "timestamp_ms": unpacked[0],
        "time_s": unpacked[1],
        "co_ppm": unpacked[2],
        "humidity": unpacked[3],
        "temperature": unpacked[4],
        "flow_rate": unpacked[5],
        "heater_voltage": unpacked[6]
    }
    return decoded

# Notification handler for binary messages
def notification_handler(sender, data: bytearray):
    try:
        decoded = decode_binary_payload(data)
        now_ms = int(time.time() * 1000)  # current time in ms
        elapsed_ms = now_ms - decoded["timestamp_ms"]

        print("\n>> Binary packet received:")
        print(f"  >> Timestamp (device): {decoded['timestamp_ms']} ms")
        print(f"  >> Received at:         {now_ms} ms")
        #print(f"  >> Elapsed time:         {elapsed_ms} ms")
        print(f"  >> Elapsed time:         {abs(elapsed_ms)} ms")
        print(f"  >> Time (s):             {decoded['time_s']:.2f}")
        print(f"  >> CO (ppm):             {decoded['co_ppm']:.2f}")
        print(f"  >> Humidity (%):        {decoded['humidity']:.2f}")
        print(f"  >> Temperature (°C):     {decoded['temperature']:.2f}")
        print(f"  >> Flow rate (mL/min):  {decoded['flow_rate']:.2f}")
        print(f"  >> Heater voltage (V):   {decoded['heater_voltage']:.2f}")

    except Exception as e:
        print(f"Failed to decode binary payload: {e}")

# Optional manual chat loop
async def chat_loop(client, characteristic_uuid):
    print(">> Type a message ('exit' to quit):")
    loop = asyncio.get_event_loop()
    while True:
        user_input = await loop.run_in_executor(None, input, "> ")
        if user_input.lower() == "exit":
            print("Exiting chat...")
            break
        try:
            await client.write_gatt_char(characteristic_uuid, user_input.encode(), response=True)
            print(">> Sent:", user_input)
        except Exception as e:
            print("Error sending message:", e)

# Main BLE flow
async def main():
    print(">> Scanning for BLE devices...")
    devices = await BleakScanner.discover(timeout=5.0)
    for d in devices:
        print(f"Found: {d.name or 'Unnamed'} ({d.address})")

    target_device = next((d for d in devices if d.name and "iPhone" in d.name), None)

    if not target_device:
        print("No suitable iPhone device found.")
        return

    print(f">> Connecting to: {target_device.name} ({target_device.address})")
    async with BleakClient(target_device.address) as client:
        print("*** Connected ***")

        # Wait for services to populate
        services = client.services
        if not services:
            print(">> Waiting for services...")
            await client._ensure_connected()  # Internal method; or use delay if needed
            services = client.services

        # Find characteristic
        char_obj = None
        for svc in services:
            if svc.uuid.lower() == SERVICE_UUID.lower():
                for char in svc.characteristics:
                    if char.uuid.lower() == CHARACTERISTIC_UUID.lower():
                        char_obj = char
                        break

        if not char_obj:
            print("Desired service/characteristic not found.")
            return

        # Enable notifications
        if "notify" in char_obj.properties or "indicate" in char_obj.properties:
            await client.start_notify(CHARACTERISTIC_UUID, notification_handler)
            print(">> Listening for binary sensor data from iOS...")

            await chat_loop(client, CHARACTERISTIC_UUID)

            await client.stop_notify(CHARACTERISTIC_UUID)
        else:
            print("This characteristic does not support notifications.")

    print("Disconnected. Goodbye!")

# Run main
if __name__ == "__main__":
    asyncio.run(main())