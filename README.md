# ZMK USB Battery Reporting

Reports keyboard battery levels over USB HID, allowing the host OS to display battery status natively.

This is particularly useful for keyboards with a dongle setup where both keyboard halves connect wirelessly to a USB dongle.

## Features

- Reports battery level as a standard USB HID report
- Supports split keyboards (reports both central and peripheral batteries)
- Works with ZMK's dongle configuration
- Automatic updates when battery level changes

## Usage

### 1. Add the module to your `config/west.yml`

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: tng-20
      url-base: https://github.com/tng-20
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-usb-reporting
      remote: tng-20
      revision: main
  self:
    path: config
```

### 2. Enable in your dongle's `.conf` file

```ini
# Enable USB battery reporting
CONFIG_ZMK_USB_HID_BATTERY_REPORTING=y

# For split keyboards, also enable peripheral battery reporting
CONFIG_ZMK_USB_HID_BATTERY_REPORTING_SPLIT=y
```

### 3. Required dependencies

Make sure your dongle configuration also has these enabled:

```ini
CONFIG_ZMK_USB=y
CONFIG_ZMK_BATTERY_REPORTING=y

# For split keyboards
CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y
```

## How It Works

The module creates a separate USB HID interface that reports battery levels:

- **Report ID 0x05**: Central/Dongle battery level (0-100%)
- **Report ID 0x06**: Peripheral battery level (0-100%) - only for split keyboards

The reports use the Generic Device Controls usage page (0x06) with Battery Strength usage (0x20), which is the standard way to report battery in USB HID.

## OS Support

| OS | Support | Notes |
|----|---------|-------|
| Linux | Expected | Via `upower` / `/sys/class/power_supply/` |
| Windows | Partial | May require additional software |
| macOS | Partial | May require additional software |

**Note**: Native OS battery display for USB HID devices varies by operating system. Some systems may require a companion application to read the HID battery reports.

## Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_ZMK_USB_HID_BATTERY_REPORTING` | `y` | Enable USB HID battery reporting |
| `CONFIG_ZMK_USB_HID_BATTERY_REPORTING_SPLIT` | `y` | Report peripheral batteries (requires split central config) |
| `CONFIG_ZMK_USB_HID_BATTERY_REPORT_INTERVAL` | `60` | Update interval in seconds |

## Architecture

```
┌─────────────────┐         ┌─────────────────┐         ┌─────────────────┐
│  Left Half      │   BLE   │     Dongle      │   USB   │       PC        │
│  (Peripheral)   │ ──────▶ │   (Central)     │ ──────▶ │                 │
│                 │         │                 │   HID   │  Battery: 85%   │
│  Battery: 92%   │         │  Battery: 100%  │         │  Battery: 92%   │
└─────────────────┘         └─────────────────┘         └─────────────────┘
                                    ▲
┌─────────────────┐   BLE           │
│  Right Half     │ ────────────────┘
│  (Peripheral)   │
│                 │
│  Battery: 88%   │
└─────────────────┘
```

## License

MIT License - see [LICENSE](LICENSE) for details.
