# MediaTek 6.12 Vendor Firmware Integration Specification (xaga / MT6895)

This directory defines the firmware layout required for MediaTek MT6895 (Dimensity 8100) devices (Redmi Note 11T Pro / POCO X4 GT / Redmi K50i) to transition from Little Kernel (LK) / Recovery stage to full Stage-2 Android init (`init`).

## Directory Hierarchy

```
vendor_firmware/
└── mediatek/
    └── mt6895/
        ├── gz/        # GenieZone Hypervisor firmware (gz.bin / gz.img)
        ├── tee/       # Trusted Execution Environment OS (tee.bin / tz.img)
        ├── sspm/      # Sensor SubSystem Power Management firmware (sspm.bin / sspm_pr.img)
        ├── mcupm/     # MicroController Unit Power Management firmware (mcupm.bin / mcupm_pr.img)
        └── pi_img/    # Power Initialization firmware image (pi_img.bin)
```

## Required Firmware Blobs for Stage 2 Android Boot

| Subsystem | Blob Name | Description | Search Path in Ramdisk |
|---|---|---|---|
| **GZ** | `gz.bin` / `gz.img` | GenieZone hypervisor image for secure virtualization | `/lib/firmware/gz.bin`, `/vendor/firmware/gz.bin` |
| **TEE** | `tee.bin` / `tz.bin` | Trusty / Kinibi TEE OS for hardware crypto & fingerprint | `/lib/firmware/tee.bin`, `/vendor/firmware/tee.bin` |
| **SSPM** | `sspm.bin` / `sspm_pr.img` | SSPM power management unit binary | `/lib/firmware/sspm.bin`, `/vendor/firmware/sspm.bin` |
| **MCUPM** | `mcupm.bin` / `mcupm_pr.img` | MCUPM power management binary | `/lib/firmware/mcupm.bin`, `/vendor/firmware/mcupm.bin` |
| **PI_IMG** | `pi_img.bin` | Power initialization table image | `/lib/firmware/pi_img.bin`, `/vendor/firmware/pi_img.bin` |

## Build Integration (`build.sh`)

During `./build.sh` image packaging (`pack_vendor`), any firmware files present in `vendor_firmware/` are automatically staged into:
- `/lib/firmware/`
- `/vendor/firmware/`

within `vendor_boot_new.img` vendor ramdisk. Kernel driver `request_firmware()` calls search these locations during early driver probe and Stage-2 boot.
