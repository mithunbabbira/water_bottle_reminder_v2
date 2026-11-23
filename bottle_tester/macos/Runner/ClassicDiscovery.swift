import Cocoa
import CoreBluetooth
import FlutterMacOS
import IOBluetooth

/// One fresh inquiry at a time. Never enumerate pairedDevices, never initiate
/// remote-name requests here, and never mistake discovery/name metadata for
/// ESP-side presence verification.
final class ClassicDiscovery: NSObject, IOBluetoothDeviceInquiryDelegate {
  private let channel: FlutterMethodChannel
  private var inquiry: IOBluetoothDeviceInquiry?
  private var completion: FlutterResult?
  private var requestID: Int?
  private var deadline: Timer?
  private var devices: [String: [String: Any]] = [:]
  private var truncated = false
  private var terminationObserver: NSObjectProtocol?

  init(messenger: FlutterBinaryMessenger) {
    channel = FlutterMethodChannel(name: "bottle/classic_discovery", binaryMessenger: messenger)
    super.init()
    channel.setMethodCallHandler { [weak self] call, result in
      // IOBluetooth inquiry and its delegate require a serviced Cocoa run loop.
      DispatchQueue.main.async {
      guard let self = self else { result(FlutterError(code: "cancelled", message: "Discovery closed.", details: nil)); return }
      let args = call.arguments as? [String: Any]
      guard let id = args?["id"] as? Int, id > 0 else {
        result(FlutterError(code: "arguments", message: "A positive request ID is required.", details: nil))
        return
      }
      switch call.method {
      case "scan": self.start(id: id, result: result)
      case "cancel":
        if self.requestID == id {
          self.finish(error: FlutterError(code: "cancelled", message: "Inquiry cancelled.", details: nil))
        }
        result(nil)
      default: result(FlutterMethodNotImplemented)
      }
      }
    }
    terminationObserver = NotificationCenter.default.addObserver(
      forName: NSApplication.willTerminateNotification, object: nil, queue: .main
    ) { [weak self] _ in
      self?.finish(error: FlutterError(code: "cancelled", message: "App closing.", details: nil))
    }
  }

  private func start(id: Int, result: @escaping FlutterResult) {
    NSLog("[ClassicDiscovery] scan request id=%d", id)
    guard inquiry == nil, completion == nil else {
      result(FlutterError(code: "busy", message: "An inquiry is already active.", details: nil))
      return
    }
    if #available(macOS 10.15, *) {
      if CBManager.authorization == .denied || CBManager.authorization == .restricted {
        result(FlutterError(code: "permission", message: "Bluetooth access is denied. Enable it in System Settings.", details: nil))
        return
      }
    }
    guard let scan = IOBluetoothDeviceInquiry(delegate: self) else {
      result(FlutterError(code: "unavailable", message: "Classic inquiry unavailable.", details: nil))
      return
    }
    devices.removeAll()
    truncated = false
    requestID = id
    completion = result
    inquiry = scan
    scan.clearFoundDevices()
    // The documented default searchType is kIOBluetoothDeviceSearchClassic.
    scan.inquiryLength = 10
    // Enabling remote-name updates can make inquiry unbounded or deadlock with
    // name requests. EIR/cached name of a FRESH found callback is identity-only.
    scan.updateNewDeviceNames = false
    deadline = Timer.scheduledTimer(withTimeInterval: 30, repeats: false) { [weak self, weak scan] _ in
      guard let self = self, let scan = scan, self.inquiry === scan else { return }
      NSLog("[ClassicDiscovery] timeout id=%d", id)
      self.finish(error: FlutterError(code: "timeout", message: "Classic inquiry exceeded 30 seconds.", details: nil))
    }
    let status = scan.start()
    NSLog("[ClassicDiscovery] start status=%d", status)
    if status != kIOReturnSuccess {
      finish(error: inquiryError(status))
    }
  }

  private func inquiryError(_ status: IOReturn) -> FlutterError {
    let code = status == kIOReturnNotPermitted || status == kIOReturnNotPrivileged
      ? "permission" : status == kIOReturnBusy ? "busy" : "radio"
    return FlutterError(code: code, message: "Classic inquiry could not complete. Check Bluetooth power and permission.", details: nil)
  }

  func deviceInquiryDeviceFound(_ sender: IOBluetoothDeviceInquiry!, device: IOBluetoothDevice!) {
    NSLog("[ClassicDiscovery] found callback")
    guard let sender = sender, inquiry === sender, completion != nil,
          let device = device, let rawAddress = device.addressString else { return }
    let address = rawAddress.replacingOccurrences(of: "-", with: ":").uppercased()
    guard address.range(of: "^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$", options: .regularExpression) != nil,
          address != "00:00:00:00:00:00", address != "FF:FF:FF:FF:FF:FF" else { return }
    // rawRSSI() is a synchronous connection-level operation, not inquiry data.
    // Do not call it inside an inquiry callback for an unconnected phone: it can
    // stall the main run loop and prevent completion/cancellation delivery.
    let rssi = -127 // unavailable, not a measured weak signal
    var name = ""
    for character in device.name ?? "" {
      if character == "\0" { continue }
      let next = String(character)
      if name.utf8.count + next.utf8.count > 48 { break }
      name += next
    }
    if let previous = devices[address]?["rssi"] as? Int, previous >= rssi { return }
    devices[address] = ["address": address, "name": name, "rssi": rssi]
    // Bound retained results even in a crowded room, retaining strongest eight.
    if devices.count > 8 {
      truncated = true
      let sorted = sortedDevices()
      devices = Dictionary(uniqueKeysWithValues: sorted.prefix(8).map { ($0["address"] as! String, $0) })
    }
  }

  func deviceInquiryComplete(_ sender: IOBluetoothDeviceInquiry!, error: IOReturn, aborted: Bool) {
    NSLog("[ClassicDiscovery] complete error=%d aborted=%d", error, aborted ? 1 : 0)
    guard let sender = sender, inquiry === sender, completion != nil else { return }
    if error != kIOReturnSuccess {
      finish(error: inquiryError(error))
    } else if aborted {
      finish(error: FlutterError(code: "cancelled", message: "Inquiry aborted.", details: nil))
    } else {
      finish(error: nil)
    }
  }

  private func sortedDevices() -> [[String: Any]] {
    return devices.values.sorted {
      let a = $0["rssi"] as! Int, b = $1["rssi"] as! Int
      return a == b ? ($0["address"] as! String) < ($1["address"] as! String) : a > b
    }
  }

  private func finish(error: FlutterError?) {
    guard let result = completion else { return }
    let id = requestID!
    let found = sortedDevices()
    let wasTruncated = truncated
    completion = nil
    requestID = nil
    deadline?.invalidate()
    deadline = nil
    let oldInquiry = inquiry
    inquiry = nil
    // Detach BEFORE stop; late/aborted callbacks cannot complete a newer scan.
    oldInquiry?.delegate = nil
    oldInquiry?.stop()
    oldInquiry?.clearFoundDevices()
    devices.removeAll()
    truncated = false
    if let error = error {
      result(error)
    } else {
      result(["id": id, "ok": true, "discovery_source": "mac_classic",
              "devices": found, "truncated": wasTruncated])
    }
  }

  deinit {
    deadline?.invalidate()
    inquiry?.delegate = nil
    inquiry?.stop()
    if let observer = terminationObserver { NotificationCenter.default.removeObserver(observer) }
    channel.setMethodCallHandler(nil)
  }
}
