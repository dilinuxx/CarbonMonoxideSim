import asyncio
from bleak import BleakScanner, BleakClient
import importlib.metadata

# Show current Bleak version
version = importlib.metadata.version("bleak")
print(">> Bleak version:", version)

SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
CHARACTERISTIC_UUID = "87654321-4321-6789-4321-0fedcba98765"

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

async def main():
    print(">> Scanning for BLE devices...")
    devices = await BleakScanner.discover(timeout=5.0)
    for d in devices:
        print(f"Found: {d.name} ({d.address})")
    target_device = next((d for d in devices if d.name and "iPhone" in d.name), None)

    if not target_device:
        print("No suitable iPhone device found.")
        return

    print(f">> Connecting to: {target_device.name} ({target_device.address})")
    async with BleakClient(target_device.address) as client:
        print("*** Connected ***")

        # Use client.services (no await needed) instead of deprecated get_services
        services = client.services
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

        # Set up notification handler
        def notification_handler(sender, data):
            try:
                message = data.decode("utf-8")
                print(f"\n>> Received from iOS: {message}")
            except Exception as e:
                print(f"\n>> (decode error): {data} - {e}")

        if "notify" in char_obj.properties or "indicate" in char_obj.properties:
            await client.start_notify(CHARACTERISTIC_UUID, notification_handler)
            print(">> Listening for incoming messages...")
        else:
            print("This characteristic does not support notifications.")

        # Begin interactive chat loop
        await chat_loop(client, CHARACTERISTIC_UUID)

        if "notify" in char_obj.properties or "indicate" in char_obj.properties:
            await client.stop_notify(CHARACTERISTIC_UUID)

    print("Disconnected. Goodbye!")

if __name__ == "__main__":
    asyncio.run(main())