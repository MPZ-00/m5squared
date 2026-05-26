"""BLE-to-sync adapter used by ECSRemote inside the GUI."""

import sys
import time
import threading
from typing import cast, Any

try:
    from m25_bluetooth_ble import M25BluetoothBLE
    HAS_BLE = True
except ImportError:
    M25BluetoothBLE = cast(Any, None)
    HAS_BLE = False


class BLEConnectionAdapter:
    """Sync adapter for the async BLE transport used by ECSRemote."""

    def __init__(self, address, key, name="wheel", debug=False, loop=None, log_callback=None):
        if not HAS_BLE:
            raise RuntimeError("BLE transport not available")
        if M25BluetoothBLE is None:
            raise RuntimeError("BLE adapter unavailable")
        self.address = address
        self.key = key
        self.name = name
        self.debug = debug
        self.loop = loop
        self.log_callback = log_callback
        self.bt = M25BluetoothBLE(address=address, key=key, name=name, debug=debug, log_callback=log_callback)
        self.connected = False
        self.notifications_started = False
        self._loop_lock = threading.RLock()

    def _trace(self, message):
        if self.log_callback:
            try:
                self.log_callback(message)
                return
            except Exception:
                pass
        if self.debug:
            print(message, file=sys.stderr)

    def _run(self, coro):
        """Run one coroutine on the shared loop, serialized across threads."""
        if not self.loop:
            return None
        with self._loop_lock:
            return self.loop.run_until_complete(coro)

    def _drain_notifications(self):
        """Flush all stale notifications before a new transact()."""
        if not self.connected:
            return
        while True:
            data = self._run(self.bt.wait_notification(timeout=0.01))
            if data is None:
                break

    @staticmethod
    def _telegram_id(packet):
        """Extract telegram ID from decrypted SPP packet."""
        if not packet or len(packet) < 2:
            return None
        return packet[1]

    def connect(self, channel=6):
        """Connect to device and enable notifications."""
        del channel
        if not self.loop:
            return False
        success = self._run(self.bt.connect(timeout=10))
        if not success:
            return False

        self.notifications_started = self._run(self.bt.start_notifications())
        if not self.notifications_started:
            self._run(self.bt.disconnect())
            return False

        self.connected = True
        time.sleep(0.1)
        return True

    def disconnect(self):
        """Disconnect from device."""
        if self.loop and self.connected:
            self._run(self.bt.disconnect())
        self.connected = False
        self.notifications_started = False

    def transact(self, spp_data, timeout=1.0):
        """Send packet and receive decrypted response (sync interface)."""
        if not self.loop or not self.connected:
            return None

        try:
            request_tid = self._telegram_id(spp_data)
            self._drain_notifications()
            ok = self._run(self.bt.send_packet(spp_data))
            if not ok:
                return None

            deadline = time.monotonic() + max(0.05, timeout)
            fallback_response = None

            while time.monotonic() < deadline:
                remaining = deadline - time.monotonic()
                wait_slice = min(0.4, max(0.01, remaining))
                response = self._run(self.bt.wait_notification(timeout=wait_slice))
                if response is None:
                    continue

                if fallback_response is None:
                    fallback_response = response

                if request_tid is None:
                    return response

                response_tid = self._telegram_id(response)
                if response_tid == request_tid:
                    return response

                if self.debug:
                    self._trace(
                        f"  [{self.name}] Ignoring out-of-order response "
                        f"tid=0x{response_tid:02X} expected=0x{request_tid:02X}"
                    )

            return fallback_response
        except Exception as e:
            if self.debug:
                self._trace(f"  transact error [{self.name}]: {e}")
            return None
