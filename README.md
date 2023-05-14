# esphome-fram
ESPHome port of [RobTillaart/FRAM_I2C](https://github.com/RobTillaart/FRAM_I2C)

The code has been adjusted to use ESPHome's implementation of the I2C bus, so the library can be used with both Arduino and ESP-IDF frameworks.

Some features were removed as I don't have use for them (yet) and can (should) be implemented separate of this component.
- removed FRAM_RINGBUFFER and FRAM_ML classes
- removed write protect pin, control that pin elsewhere in esphome
- removed hard-coded size for FRAM9/FRAM11, set the size in yaml

## Usage

```yaml
external_components:
  - source: github://sharkydog/esphome-fram
    components: [ fram ]

i2c:
  scl: 10
  sda: 8
  id: i2c_1

fram:
 - id: fram_1
 - id: fram_2
   i2c_id: i2c_1
   address: 0x52
   type: FRAM11
   size: 2KiB

some_option:
  on_something:
    - lambda: |-
        uint8_t write_bytes[3] = {0xA2,0xB2,0xC2};
        fram_1->write(0x0001, write_bytes, 3);
        
        uint8_t read_bytes[2] = {0,0};
        fram_1->read(0x0002, read_bytes, 2);
        ESP_LOGD("fram","Bytes: 0x%X 0x%X", read_bytes[0], read_bytes[1]);
```

Multiple devices can be used at the same time.
- **id** - Unique ID for use in lambdas
- **i2c_id** - (*optional*) ID of the I2C bus
- **address** - (*optional*, *default 0x50*) I2C address
- **type** - (*optional*, *default FRAM*) One of: **FRAM**, **FRAM9**, **FRAM11**, **FRAM32**
- **size** - (*optional*) Set the fram size, if your device does not return a size and this is not set, logs will warn you
  - valid option is a number optionally followed by suffix - one of: B, KB, KiB, MB, MiB
  - KB and MB multiply by 1000, KiB and MiB multiply by 1024
  - you won't be able to use clear() method if size is unknown

**I only have MB85RC256V, it has no sleep function, so my `FRAM9/FRAM11/FRAM32` and `FRAM::sleep()` are not tested**.

Fore more info on methods and supported devices, see [RobTillaart/FRAM_I2C/README.md](https://github.com/RobTillaart/FRAM_I2C/blob/master/README.md)

## fram_pref - global_preferences handler
A component that replaces global_preferences, meaning wherever there is a setting "restore from flash" or similar, those states will be written in FRAM.

Tested with a Switch on ESP8266 and ESP32-C3 with ESP-IDF.

All preferences will be wiped out on reflash.
A more persistant option (to survive reflash, config change, etc) may be added in the future, when I figure out how it should work (help is always wellcome).

```yaml
external_components:
  - source: github://sharkydog/esphome-fram
    components: [ fram, fram_pref ]

i2c:
  scl: 10
  sda: 8
  id: i2c_1

fram:
 - id: fram_1

fram_pref:
  fram_id: fram_1
  pool_size: 1KiB
  pool_start: 100

switch:
  - platform: gpio
    pin: 12
    name: "test switch"
    id: switch_1
    restore_mode: RESTORE_DEFAULT_OFF
```
- **pool_size** - (**_required_**) Size of the pool to hold preferences, min 9, max 65535 (64KiB)
- **pool_start** - (*optional*, *default 0*) Starting address for the pool
