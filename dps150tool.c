/*****************************************************************************
 *
 * Copyright (c) 2025 Sven Kreiensen
 * All rights reserved.
 *
 * You can use this software under the terms of the MIT license
 * (see LICENSE.md).
 *
 * THE SOFTWARE IS PROVIDED .AS IS., WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************/
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define HEADER_INPUT 0xF0
#define HEADER_OUTPUT 0xF1

#define CMD_READ 0xA1
#define CMD_WRITE 0xB1
#define CMD_BAUD 0xB0
#define CMD_SESSION 0xC1

#define REG_W_VOLTAGE 0xC1
#define REG_W_CURRENT 0xC2
#define REG_OUTPUT_VIP 0xC3
#define REG_W_OUTPUT 0xDB
#define REG_MODEL 0xDE
#define REG_HW_VERSION 0xDF
#define REG_FW_VERSION 0xE0
#define REG_W_METERING 0xD8
#define REG_W_OVP 0xD1
#define REG_W_OCP 0xD2
#define REG_W_OPP 0xD3
#define REG_W_OTP 0xD4
#define REG_W_LVP 0xD5
#define REG_DEVICE_ADDR 0xE1

#define SOFTWARE_VERSION "1.0"

/**
 * Check if a specific bit is set in an integer value
 * @param value The integer value to check
 * @param bit The bit position to check (0-based, 0 = least significant bit)
 * @return 1 if bit is set, 0 otherwise
 */
static inline int is_bit_set(int value, int bit) {
  return (value & (1 << bit)) != 0;
}

int serial_fd;
char *device = "/dev/ttyUSB0";
int debug; // Debug flag

/**
 * Opens the serial port
 * @param device The device to open
 * @return 0 on success, -1 on error
 */
int open_serial(const char *device) {
  struct termios options;

  serial_fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
  if (serial_fd == -1) {
    perror("Error opening serial port");

    return -1;
  }

  tcgetattr(serial_fd, &options);
  cfsetispeed(&options, B115200);
  cfsetospeed(&options, B115200);
  options.c_cflag = CS8 | CLOCAL | CREAD;
  options.c_iflag = IGNPAR;
  options.c_oflag = 0;
  options.c_lflag = 0;
  tcflush(serial_fd, TCIFLUSH);
  tcsetattr(serial_fd, TCSANOW, &options);

  return 0;
}

/**
 * Send command
 * @param c1 The first command byte
 * @param c2 The second command byte
 * @param c3 The third command byte
 * @param c4 The fourth command byte
 * @param c5 The fifth command byte
 * @return 0 on success, -1 on error
 */
void send_command(uint8_t c1, uint8_t c2, uint8_t c3, uint8_t *c5, uint8_t c4) {
  uint8_t c6 = c3 + c4;

  for (int i = 0; i < c4; i++) {
    c6 += c5[i];
  }

  uint8_t command[c4 + 5];
  command[0] = c1;
  command[1] = c2;
  command[2] = c3;
  command[3] = c4;
  memcpy(&command[4], c5, c4);
  command[4 + c4] = c6;
  
  if (debug) {
      printf("Sent: %02X %02X %02X %02X ", command[0], command[1], command[2], command[3]);
      for(int i=0; i<command[3]; i++) printf("%02X ", command[4+i]);
      printf("%02X\n", command[4+command[3]]);
  }
  
  (void)write(serial_fd, command, sizeof(command));
  usleep(50000);
}

/**
 * Print printable characters from buffer
 * @param label The label to print
 * @param data The data to print
 * @param length The length of the data
 */
void print_printable_string(const char *label, uint8_t *data, int length) {
  printf("%s", label);

  for (int i = 0; i < length; i++) {
    if (data[i] >= 0x20 && data[i] <= 0x7E) {
      printf("%c", data[i]);
    }
  }
  
  printf("\n");
}

/**
 * Parse response
 * @param response_type The type of response to parse
 */
void receive_response(int response_type) {
  uint8_t buffer[1024];

  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(serial_fd, &read_fds);
  
  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 100000; // 100ms
  
  int ret = select(serial_fd + 1, &read_fds, NULL, NULL, &timeout);
  if (ret <= 0) return; // Timeout or error

  int bytes_read = read(serial_fd, buffer, sizeof(buffer));
  if (bytes_read > 0) {
    if (debug) {
      printf("Received %d bytes\n", bytes_read);
    }
    for (int i = 0; i < bytes_read - 4; i++) {
      if (buffer[i] == HEADER_INPUT && buffer[i+1] == CMD_READ) {
        uint8_t cmd_type = buffer[i+2];
        uint8_t payload_len = buffer[i+3];
        if (i + 5 + payload_len <= bytes_read) {
          switch (cmd_type) {
          case REG_W_VOLTAGE:
            {
              float value;
              memcpy(&value, &buffer[i+4], 4);
              printf("Output Voltage: %.2fV\n", value);
            }
            break;
          case REG_W_CURRENT:
            {
              float value;
              memcpy(&value, &buffer[i+4], 4);
              printf("Output Current: %.2fA\n", value);
            }
            break;
          case REG_OUTPUT_VIP:
            break;
          case 222:
            print_printable_string("Device Model: ", &buffer[i+4], payload_len);
            break;
          case 223:
            print_printable_string("Hardware Version: ", &buffer[i+4], payload_len);
            break;
          case 224:
            print_printable_string("Firmware Version: ", &buffer[i+4], payload_len);
            break;
          case 225:
            printf("Device ID: %d\n", buffer[i+4]);
            break;
          case 255:
            if (payload_len >= 139) {
              float v, a, w;
              memcpy(&v, &buffer[i+4 + 12], 4);
              memcpy(&a, &buffer[i+4 + 16], 4);
              memcpy(&w, &buffer[i+4 + 20], 4);
              if (is_bit_set(response_type, 0)) printf("Output Voltage: %.2f V\n", v);
              if (is_bit_set(response_type, 1)) printf("Output Current: %.3f A\n", a);
              if (is_bit_set(response_type, 2)) printf("Output Power: %.2f W\n", w);
            }
            break;
          case 192:
          case 196:
            break;
          }
          i += (4 + payload_len); // skip the parsed packet
        }
      }
    }
  }
}

/**
 * Send float value i.e. voltages, currents,...
 * @param type The type of value to send
 * @param value The value to send
 */
void set_float_value(uint8_t type, float value) {
  uint8_t data[4];

  memcpy(data, &value, sizeof(float));
  send_command(HEADER_OUTPUT, CMD_WRITE, type, data, 4);
}

/* Send byte value i.e. on/off flags */
void set_byte_value(uint8_t type, uint8_t value) {
  uint8_t data[1] = {value};

  send_command(HEADER_OUTPUT, CMD_WRITE, type, data, 1);
}

/**
 * Enable output
 */
void enable_output() { 
    set_byte_value(REG_W_OUTPUT, 1); 
}

/**
 * Disable output
 */
void disable_output() { 
    set_byte_value(REG_W_OUTPUT, 0); 
}

/**
 * Set over-voltage protection
 * @param value The voltage limit
 */
void set_ovp(float value) { 
    set_float_value(REG_W_OVP, value); 
}

/**
 * Set over-current protection
 * @param value The current limit
 */
void set_ocp(float value) { 
    set_float_value(REG_W_OCP, value); 
}

/**
 * Set over-power protection
 * @param value The power limit
 */
void set_opp(float value) { 
    set_float_value(REG_W_OPP, value); 
}

/**
 * Set over-temperature protection
 * @param value The temperature limit
 */
void set_otp(float value) { 
    set_float_value(REG_W_OTP, value); 
}

/**
 * Set low-voltage protection
 * @param value The low voltage limit
 */
void set_lvp(float value) { 
    set_float_value(REG_W_LVP, value); 
}

/**
 * Recall preset memory (M1-M6)
 * Note: To truly recall, the official app reads the preset register
 * and sends it as set voltage and set current. For this CLI tool,
 * we will send a read request to the preset registers and let receive_response handle it.
 * But since receive_response isn't designed to send secondary commands,
 * an atomic CLI tool is better suited to just instructing the user to read the preset and apply manually,
 * or we simply don't support true "recall", just reading it.
 * However, since we can parse it, let's just send the read command.
 */
void read_preset(int preset) {
    if (preset < 1 || preset > 6) return;
    int v_reg = 0xC3 + (preset * 2);
    send_command(HEADER_OUTPUT, CMD_READ, v_reg, NULL, 0);
    send_command(HEADER_OUTPUT, CMD_READ, v_reg + 1, NULL, 0);
}

void write_preset(int preset, float voltage, float current) {
    if (preset < 1 || preset > 6) return;
    int v_reg = 0xC3 + (preset * 2);
    set_float_value(v_reg, voltage);
    set_float_value(v_reg + 1, current);
}

/**
 * Get model name
 */
void get_model_name() {
  send_command(HEADER_OUTPUT, CMD_READ, REG_MODEL, 0, 0);
}

/**
 * Get hardware version
 */
void get_hardware_version() {
  send_command(HEADER_OUTPUT, CMD_READ, REG_HW_VERSION, 0, 0);
}

/**
 * Get firmware version
 */
void get_firmware_version() {
  send_command(HEADER_OUTPUT, CMD_READ, REG_FW_VERSION, 0, 0);
}

/**
 * Initialize device communication
 */
void init_device() {
  send_command(HEADER_OUTPUT, CMD_SESSION, 0, (uint8_t[]){1}, 1);
  uint8_t baudrate_index = 4; // 115200 baud
  send_command(HEADER_OUTPUT, CMD_BAUD, 0, &baudrate_index, 1);
  for(int i=0; i<10; i++) receive_response(1); // Drain buffer
  
  // Verify device presence
  send_command(HEADER_OUTPUT, CMD_READ, REG_DEVICE_ADDR, NULL, 0);
  
  uint8_t buffer[1024];
  int found = 0;
  for (int i = 0; i < 10; i++) {
      int bytes_read = read(serial_fd, buffer, sizeof(buffer));
      if (bytes_read > 2) {
          for (int j = 0; j < bytes_read - 2; j++) {
              if (buffer[j] == HEADER_INPUT && buffer[j+1] == CMD_READ && buffer[j+2] == REG_DEVICE_ADDR) {
                  found = 1;
                  break;
              }
          }
      }
      if (found) break;
  }
  
  if (!found) {
      fprintf(stderr, "Error: Device did not respond to initialization handshake.\n");
      close(serial_fd);
      exit(EXIT_FAILURE);
  }
}

/**
 * Close the serial port
 * @param disconnect Whether to disconnect the device
 */
void close_serial(int disconnect) {
  if (disconnect) {
      send_command(HEADER_OUTPUT, CMD_SESSION, 0, (uint8_t[]){0}, 1);
  }
  close(serial_fd);
}

/**
 * Print usage
 */
void usage(const char *program_name) {
  fprintf(stderr,
          "Usage: %s [-d device] [-u voltage] [-i current] [-o 0|1]\n"
          "       Protections: [-x ovp] [-y ocp] [-X opp] [-Y otp] [-L lvp]\n"
          "       Presets: [-p 1-6] (Use with -u/-i to write, alone to read)\n"
          "       Readings: [-U] [-I] [-P] [-V]\n"
          "       Options: [-z] (no disconnect) [-v] (verbose)\n"
          "Version: %s\n",
          program_name, SOFTWARE_VERSION);
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
  float voltage = -1.0, current = -1.0;
  float ovp = -1.0, ocp = -1.0, opp = -1.0, otp = -1.0, lvp = -1.0;
  int output = -1, get_voltage = 0, get_current = 0, get_power = 0, get_info = 0;
  int preset = -1;
  int opt;
  int disconnect = 1;
  debug = 0;

  if (argc == 1) {
    usage(argv[0]);
  }

  while ((opt = getopt(argc, argv, "d:u:i:x:y:X:Y:L:p:UIPVo:zv")) != -1) {
    switch (opt) {
    case 'd':
      device = optarg;
      break;
    case 'u':
      voltage = atof(optarg);
      break;
    case 'i':
      current = atof(optarg);
      break;
    case 'x':
      ovp = atof(optarg);
      break;
    case 'y':
      ocp = atof(optarg);
      break;
    case 'X':
      opp = atof(optarg);
      break;
    case 'Y':
      otp = atof(optarg);
      break;
    case 'L':
      lvp = atof(optarg);
      break;
    case 'p':
      preset = atoi(optarg);
      break;
    case 'U':
      get_voltage = 1;
      break;
    case 'I':
      get_current = 2;
      break;
    case 'P':
      get_power = 4;
      break;
    case 'V':
      get_info = 1;
      break;
    case 'o':
      output = atoi(optarg);
      break;
    case 'z':
      disconnect = 0;
      break;
    case 'v':
      debug = 1;
      break;
    default:
      usage(argv[0]);
    }
  }

  if (open_serial(device) != 0) {
    return 1;
  }

  init_device();

  // The firmware ignores commands sent too quickly after connecting
  usleep(1500000); // 1.5s connection settling delay

  // 1. Set Protections First
  int protections_changed = 0;
  if (ovp >= 0.0) { set_ovp(ovp); protections_changed = 1; }
  if (ocp >= 0.0) { set_ocp(ocp); protections_changed = 1; }
  if (opp >= 0.0) { set_opp(opp); protections_changed = 1; }
  if (otp >= 0.0) { set_otp(otp); protections_changed = 1; }
  if (lvp >= 0.0) { set_lvp(lvp); protections_changed = 1; }

  // Give the device's internal task scheduler time to apply the new protection 
  // limits before we potentially exceed the old ones with set_voltage
  if (protections_changed) {
      usleep(300000); // 300ms
  }

  // 2. Handle Presets
  if (preset >= 1 && preset <= 6) {
    if (voltage >= 0.0 && current >= 0.0) {
      write_preset(preset, voltage, current);
    } else {
      read_preset(preset);
      for(int i=0; i<10; i++) receive_response(3);
    }
  }

  // 3. Set Live Voltage/Current (if not handled by presets)
  int v_c_changed = 0;
  if (voltage >= 0.0 && preset == -1) {
    set_float_value(REG_W_VOLTAGE, voltage);
    v_c_changed = 1;
  }
  if (current >= 0.0 && preset == -1) {
    set_float_value(REG_W_CURRENT, current);
    v_c_changed = 1;
  }

  // Workaround for device bug where first commands after POR/output-off might be ignored.
  // We send the setpoints a second time, just like the official app and Python driver do.
  if (v_c_changed) {
      usleep(500000); // 500ms delay between first and second send
      if (voltage >= 0.0 && preset == -1) {
          set_float_value(REG_W_VOLTAGE, voltage);
      }
      if (current >= 0.0 && preset == -1) {
          set_float_value(REG_W_CURRENT, current);
      }
  }

  // Allow the device to lock in the target voltage/current before turning on output.
  // Turning it on instantly after setting it trips REG_W_OCP on the first boot because
  // the CC loop needs time to apply the new limits.
  if (v_c_changed && output == 1) {
      usleep(500000); // 500ms DAC settling delay
  }

  // 4. Enable/Disable Output
  if (output == 1)
    enable_output();
  if (output == 0)
    disable_output();

  if (get_voltage || get_current || get_power) {
    uint8_t flags = get_voltage + get_current + get_power;
    usleep(500000); // 500ms for power supply to physically respond
    send_command(HEADER_OUTPUT, CMD_READ, 255, (uint8_t[]){0}, 1);
    usleep(50000); // 50ms to allow all 144 bytes to arrive in OS buffer
    for(int i=0; i<20; i++) receive_response(flags);
  }
  
  if (get_info) {
    get_model_name();
    for(int i=0; i<5; i++) receive_response(7);
    get_hardware_version();
    for(int i=0; i<5; i++) receive_response(7);
    get_firmware_version();
    for(int i=0; i<5; i++) receive_response(7);
  }

  close_serial(disconnect);
  return 0;
}
