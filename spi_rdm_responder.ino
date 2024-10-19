/**
 * This example provides a very simple RDM responder using the TimoTwo module's SPI interface.
 * 
 * This example does not implements any exhaustive error checking. This must be 
 * added in a real-world implementation.
 */

#include <SPI.h>
#include "timo_spi.h"
#include "e120.h"

#define  MIN( A,  B )      ( ( A )  <  ( B )   ?   ( A )   :   ( B ) )

#define TIMO_IDENTIFY_IRQ (1 << 5)

#define TIMO_SPI_DEVICE_BUSY_IRQ_MASK (1 << 7)

static timo_t timo = { .csn_pin =  5, .irq_pin = 3 };

/* SPI communication buffers, make sure these are aligned to 32 bit words */
static uint32_t tx_buffer32[256];
static uint32_t rx_buffer32[256];
/* point the working byte pointers to the aligned buffers */
static uint8_t* tx_buffer = (uint8_t*)tx_buffer32;
static uint8_t* rx_buffer = (uint8_t*)rx_buffer32;

static bool has_set_up = false;

static bool should_update_dmx_window = false;

/* Responder data below */

/* The UID to use for this responder, this should be changed in real-life applications */
static Uid my_uid = {0x4c55, 0x11223344};

/* DMX settings */
static uint16_t dmx_start_addr = 0;
static uint16_t dmx_footprint = 512;

/* Default device label */
static uint8_t device_label_length = 4;
static char device_label[32] = {'T', 'e', 's', 't'};

static bool is_muted = false;
static bool is_identifying = false;

/* This list contains the RDM PIDs that the responder supports on top of the required ones from E1.20 standard */
#define N_SUPPORTED_PIDS 			3
const uint16_t supportedPidList[N_SUPPORTED_PIDS] = { DEVICE_MODEL_DESCRIPTION, MANUFACTURER_LABEL, DEVICE_LABEL };

void irq_pin_handler() {
  timo.irq_pending = 1;
}

bool irq_is_pending() {
  noInterrupts();
  bool pending = timo.irq_pending;
  timo.irq_pending = false;
  interrupts();
  return pending;
}

/**
 * This is the Arduino setup function, it's called when the Arduino starts up 
 */
void setup() {

  /* Initiate serial port to 250 kbps */
  Serial.begin(250000);

  /* Initiate SPI */
  SPI.begin();

  /* Setup IRQ and CS pins */
  pinMode(timo.irq_pin, INPUT);
  pinMode(timo.csn_pin, OUTPUT);
  digitalWrite(timo.csn_pin, HIGH);

  attachInterrupt(digitalPinToInterrupt(timo.irq_pin), irq_pin_handler, FALLING);

  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

  delay(1000);

  /* Clear the Serial port from any garbage bytes */
  while(Serial.available() > 0) {
    Serial.read();
  }

  Serial.println("Running");
  Serial.flush();

  /* Wait here until module has booted and IRQ signal is high */
  while (irq_is_pending()) {
    ;
  }
}

/**
 * This function updates the radio module's DMX_WINDOW function according to the DMX settings. 
 */
void update_dmx_window() {
    int16_t irq_flags;
    Serial.println("DMX window:");
    tx_buffer[0] = dmx_footprint >> 8;
    tx_buffer[1] = dmx_footprint;
    tx_buffer[2] = dmx_start_addr >> 8;
    tx_buffer[3] = dmx_start_addr;
    irq_flags = timo_transfer(TIMO_WRITE_REG_COMMAND(TIMO_DMX_WINDOW_REG), rx_buffer, tx_buffer, 5);
    irq_flags = timo_transfer(TIMO_READ_REG_COMMAND(TIMO_DMX_WINDOW_REG), rx_buffer, tx_buffer, 5);
    print_response(irq_flags, rx_buffer, 4);
}

/**
 * This is the Arduino main loop function, it's called repeatedly 
 */
void loop() {
  int16_t irq_flags;

  /* If we have not configured the radio module, do so now. */
  if (!has_set_up) {

    Serial.println("Version:");
    irq_flags = timo_transfer(TIMO_READ_REG_COMMAND(TIMO_VERSION_REG), rx_buffer, tx_buffer, 9);
    print_response(irq_flags, rx_buffer, 8);

    Serial.println("Config:");
    irq_flags = timo_transfer(TIMO_READ_REG_COMMAND(TIMO_CONFIG_REG), rx_buffer, tx_buffer, 2);
    print_response(irq_flags, rx_buffer, 1);
    while (rx_buffer[0] & TIMO_CONFIG_RADIO_TX_RX_MODE) {
      Serial.println("In TX mode - changing mode");
      /* in TX mode - change to RX */
      tx_buffer[0] = rx_buffer[0] & ~TIMO_CONFIG_RADIO_TX_RX_MODE;
      timo_transfer(TIMO_WRITE_REG_COMMAND(TIMO_CONFIG_REG), rx_buffer, tx_buffer, 2);
      delay(3000);
      irq_flags = timo_transfer(TIMO_READ_REG_COMMAND(TIMO_CONFIG_REG), rx_buffer, tx_buffer, 2);
      print_response(irq_flags, rx_buffer, 1);
    }

    /* Enable SPI RDM */
    tx_buffer[0] = 0x89;
    timo_transfer(TIMO_WRITE_REG_COMMAND(TIMO_CONFIG_REG), rx_buffer, tx_buffer, 2);
    irq_flags = timo_transfer(TIMO_READ_REG_COMMAND(TIMO_CONFIG_REG), rx_buffer, tx_buffer, 2);
    print_response(irq_flags, rx_buffer, 1);

    /* Inform the radio module about our RDM UID. This is required. */
    Serial.println("Binding UID:");
    tx_buffer[0] = my_uid.mId >> 8;
    tx_buffer[1] = my_uid.mId;
    tx_buffer[2] = my_uid.devId >> 24;
    tx_buffer[3] = my_uid.devId >> 16;
    tx_buffer[4] = my_uid.devId >> 8;
    tx_buffer[5] = my_uid.devId;
    timo_transfer(TIMO_WRITE_REG_COMMAND(TIMO_BINDING_UID_REG), rx_buffer, tx_buffer, 7);
    print_response(irq_flags, rx_buffer, 1);

    Serial.println("Status:");
    irq_flags = timo_transfer(TIMO_READ_REG_COMMAND(TIMO_STATUS_REG), rx_buffer, tx_buffer, 2);
    print_response(irq_flags, rx_buffer, 1);

    /* Set default DMX_WINDOW settings */
    update_dmx_window();

    /* We want IRQ when radio link status change, when DMX change, or extended interrupts */
    Serial.println("IRQ mask:");
    tx_buffer[0] = TIMO_IRQ_RF_LINK_FLAG | TIMO_IRQ_DMX_CHANGED_FLAG | TIMO_IRQ_EXTENDED_FLAG;
    irq_flags = timo_transfer(TIMO_WRITE_REG_COMMAND(TIMO_IRQ_MASK_REG), rx_buffer, tx_buffer, 2);
    irq_flags = timo_transfer(TIMO_READ_REG_COMMAND(TIMO_IRQ_MASK_REG), rx_buffer, tx_buffer, 2);
    print_response(irq_flags, rx_buffer, 1);

    /* For extended interrupt we want IRW for RDM requests */
    Serial.println("Extended IRQ mask:");
    tx_buffer[0] = 0;
    tx_buffer[1] = 0;
    tx_buffer[2] = 0;
    tx_buffer[3] = TIMO_EXTIRQ_SPI_RDM_FLAG;
    irq_flags = timo_transfer(TIMO_WRITE_REG_COMMAND(TIMO_EXT_IRQ_MASK_REG), rx_buffer, tx_buffer, 5);
    irq_flags = timo_transfer(TIMO_READ_REG_COMMAND(TIMO_EXT_IRQ_MASK_REG), rx_buffer, tx_buffer, 5);
    print_response(irq_flags, rx_buffer, 4);

    has_set_up = true;

  }

  /* If DMX settings has changed, update the DMX_WINDOW settigs in the radio module */
  if (should_update_dmx_window) {
    update_dmx_window();
    should_update_dmx_window = false;
  }

  /* if there is a pending IRQ */
  if (!digitalRead(timo.irq_pin)) {
    /* Send NOP command to read the IRQ flags */
    irq_flags = timo_transfer(TIMO_NOP_COMMAND, rx_buffer, tx_buffer, 0);

    /* Wait for IRQ signal to go high again - this indicates our command has been processed */
    while (!irq_is_pending()) {
      ;
    }

    /* If the RF link status has changed, we read the status to clear the IRQ and print the status */
    if (irq_flags & TIMO_IRQ_RF_LINK_FLAG) {
      irq_flags = timo_transfer(TIMO_READ_REG_COMMAND(TIMO_STATUS_REG), rx_buffer, tx_buffer, 2);
      Serial.println("RF Link");
      print_response(irq_flags, rx_buffer, 1);
    }

    /* If DMX has changed, we read the DMX data. This will also clear the IRQ. */
    if (irq_flags & TIMO_IRQ_DMX_CHANGED_FLAG) {
      irq_flags = timo_transfer(TIMO_READ_DMX_COMMAND, rx_buffer, tx_buffer, dmx_footprint+1);
      Serial.println("DMX data");
      print_response(irq_flags, rx_buffer, dmx_footprint);
    }

    /* If there is a extended IRQ we need to check what to do */
    if (irq_flags & TIMO_IRQ_EXTENDED_FLAG) {
      uint32_t ext_flags;
      uint8_t response_length;
      bzero(tx_buffer, 5);
      /* Read the extended IRQ flags */
      irq_flags = timo_transfer(TIMO_READ_REG_COMMAND(TIMO_EXT_IRQ_FLAGS_REG), rx_buffer, tx_buffer, 5);
      ext_flags = ((uint32_t)rx_buffer[0] << 24) | ((uint32_t)rx_buffer[1] << 16) | ((uint32_t)rx_buffer[2] << 8) | ((uint32_t)rx_buffer[3]);
      
      /* Check if we got an RDM request */
      if (ext_flags & TIMO_EXTIRQ_SPI_RDM_FLAG) {
        while (!irq_is_pending()) {
          ;
        }
        /* Read the RDM request */
        bzero(tx_buffer, 255);
        timo_transfer_rdm_request(TIMO_READ_RDM_COMMAND, rx_buffer, tx_buffer, 255);
        Serial.println("RDM request");
        print_response(irq_flags, rx_buffer, rx_buffer[2]+2);

        /* Process the RDM request and check if we have a response to return */
        response_length = parseRequest((RdmRequest*)rx_buffer);

        /* If response length is > 0 it means we have a response to return */
        if (response_length > 0) {
          delay(1);
          /* Write the RDM response to the radio module */
          timo_transfer(TIMO_WRITE_RDM_COMMAND, tx_buffer, rx_buffer, response_length+1); // buffers swapped since response is in rx_buffer
          Serial.println("RDM response");
        }
      }
    }
  }
}

/**
 * Makes a complete SPI transaction with the TimoTwo module
 *
 * Return value:   The content of the IRQ flags register, or -1 if there was no response.
 *
 * @param command   The TimoTwo SPI command.
 * @param *dst      Pointer to the buffer where to store the returned data
 * @param *src      Pointer to the buffer containing data to transfer
 * @param len       Length in bytes. IRQ flags is included. Example: Use length 9 when reading the version register.
 */
int16_t timo_transfer(uint8_t command, uint8_t *dst, uint8_t *src, uint32_t len) {
  uint8_t irq_flags;

  uint32_t start_time = millis();

  /* Perform the transfer of the command byte */
  digitalWrite(timo.csn_pin, LOW);
  irq_flags = SPI.transfer(command);
  irq_is_pending();
  digitalWrite(timo.csn_pin, HIGH);

  /* If no bytes to transfer, this was a NOP command - just wait for IRQ or timeout */
  if (len == 0) {
    start_time = millis();
    while ((!digitalRead(timo.irq_pin)) && (!irq_is_pending())) {
      if (millis() - start_time > 10) {
        break;
      }
    }
    return irq_flags;
  }

  /* wait for IRQ or timeout */
  while(!irq_is_pending()) {
    if (millis() - start_time > 1000) {
      return -1;
    }
  }

  /* start the payload transfer */
  digitalWrite(timo.csn_pin, LOW);
  irq_flags = SPI.transfer(0xff);

  /* If busy flag is set we can't do the transfer now, cancel */
  if (irq_flags & TIMO_SPI_DEVICE_BUSY_IRQ_MASK) {
    digitalWrite(timo.csn_pin, HIGH);
    return irq_flags;
  }

  /* Transfer the data */
  for (uint32_t i = 0; i < len - 1; i++) {
    *dst++ = SPI.transfer(*src++);
  }

  /* End transfer */
  digitalWrite(timo.csn_pin, HIGH);

  /* wait for IRQ or timeout */
  while (!digitalRead(timo.irq_pin)) {
    if (millis() - start_time > 50) {
      break;
    }
  }
  return irq_flags;
}

/**
 * Makes a complete SPI transaction that takes the RDM request length into consideration.
 *
 * Return value:   The content of the IRQ flags register, or -1 if there was no response.
 *
 * @param command   The TimoTwo SPI command.
 * @param *dst      Pointer to the buffer where to store the returned data
 * @param *src      Pointer to the buffer containing data to transfer
 * @param max_len   Maximum length in bytes. The actual length is checked with the length field in RDM request.
 */
int16_t timo_transfer_rdm_request(uint8_t command, uint8_t *dst, uint8_t *src, uint32_t max_len) {
  uint8_t irq_flags;

  uint32_t start_time = millis();

  /* Perform the transfer of the command byte */
  digitalWrite(timo.csn_pin, LOW);
  irq_flags = SPI.transfer(command);
  irq_is_pending();
  digitalWrite(timo.csn_pin, HIGH);

  /* If no bytes to transfer, this was a NOP command - just wait for IRQ or timeout */
  if (max_len == 0) {
    start_time = millis();
    while ((!digitalRead(timo.irq_pin)) && (!irq_is_pending())) {
      if (millis() - start_time > 10) {
        break;
      }
    }
    return 0;
  }

  /* wait for IRQ or timeout */
  while(!irq_is_pending()) {
    if (millis() - start_time > 1000) {
      return -1;
    }
  }

  /* start the payload transfer */
  digitalWrite(timo.csn_pin, LOW);
  irq_flags = SPI.transfer(0xff);

  /* If busy flag is set we can't do the transfer now, cancel */
  if (irq_flags & TIMO_SPI_DEVICE_BUSY_IRQ_MASK) {
    digitalWrite(timo.csn_pin, HIGH);
    return 0;
  }

  /* Transfer the data */
  for (uint32_t i = 0; i < max_len - 1; i++) {
    uint8_t data;
    data = SPI.transfer(*src++);
    if (i == 2) {
      /* At this position in the RDM request the length (excluding checksum), use this as the correct length to read */
      if ((data + 2) < (max_len - 1)) {
        max_len = data + 2 + 1;
      }
    }
    *dst++ = data;
  }

  /* End transfer */
  digitalWrite(timo.csn_pin, HIGH);

  /* wait for IRQ or timeout */
  while (!digitalRead(timo.irq_pin)) {
    if (millis() - start_time > 50) {
      break;
    }
  }
  return max_len - 1;
}

/**
 * Converts a ASCII character as a single HEX digit to integer.
 *
 * Return value:   The value, 0-15.
 *
 * @param ascii    The hex character.
 */
uint8_t hex_nibble_to_val(char ascii) {
  if (ascii >= '0' && ascii <= '9') {
    return ascii - '0';
  } else if (ascii >= 'A' && ascii <= 'F') {
    return ascii - 'A' + 10;
  } else if (ascii >= 'a' && ascii <= 'f') {
    return ascii - 'a' + 10;
  }
  Serial.print("! ");
  Serial.print("0x");
  Serial.print(ascii, HEX);
  Serial.println(" is not a valid hex symbol");
  return 0;
}

/**
 * Converts a an integer 0-15 to ASCII character as a single HEX digit.
 *
 * Return value:   The hex digit as ASCII char, '0'-'F'.
 *
 * @param nibble   The integer.
 */
char nibble_to_hex(uint8_t nibble) {
  nibble &= 0x0f;
  if (nibble >= 0 && nibble <= 9) {
    return '0' + nibble;
  } else {
    return 'A' + nibble - 10;
  }
}

/**
 * Prints the response from the module as hex, including the IRQ flags.
 *
 * @param irq_flags  The IRQ flags
 * @param *data      Pointer to the data
 * @param len        Length of data
 */
void print_response(int16_t irq_flags, uint8_t *data, uint32_t len) {
  if (irq_flags < 0) {
    Serial.println("! Timeout");
    return;
  }
  Serial.print("< ");
  print_irq_flags(irq_flags);
  Serial.print(" ");
  Serial.flush();
  for (uint32_t i = 0; i < len; i++) {
    Serial.print(nibble_to_hex(0x0f & (data[i] >> 4)));
    Serial.print(nibble_to_hex(0x0f & data[i]));
    Serial.print(" ");
    Serial.flush();
  }
  Serial.println();
}

/**
 * Prints IRQ flags.
 *
 * @param irq_flags  The IRQ flags
 */
void print_irq_flags(int16_t irq_flags) {
  for (int i = 7; i >= 0; i--) {
    if (irq_flags & (1 << i)) {
      Serial.print('1');
    } else {
      Serial.print('0');
    }
  }
  Serial.flush();
}

/*
 * RDM functions below
 */

/**
 * Returns the manufacturer ID part of UID.
 *
 * @param uid  The RDM UID
 */
static uint16_t mId(uint8_t* uid) {
  return (uid[0] << 8) | uid[1];
}

/**
 * Returns the device ID part of UID.
 *
 * @param uid  The RDM UID
 */
static uint32_t devId(uint8_t* uid) {
  return (uid[2] << 24) | (uid[3] << 16) | (uid[4] << 8) | uid[5];
}

/**
 * Checks if destination is a broadcast (or vendorcast) address.
 *
 * @param destination  The RDM UID
 */
static bool isBroadcast(Uid* destination) {
  if (destination->devId == 0xFFFFFFFF) {
    return true;
  } else {
    return false;
  }
}

/**
 * Checks if destication is our UID.
 *
 * @param destination  The RDM UID
 */
static bool isToMe(Uid* destination) {
  if (destination->mId == my_uid.mId) {
 	  if ((destination->devId == my_uid.devId) || (destination->devId == 0xFFFFFFFF)) {
      return true;
    }
  }
  return false;
}

/**
 * Calculate an RDM message's checksum.
 *
 * @param message  Pointer to RDM message
 */
static uint16_t calculateChecksum(RdmMessage* message) {
  uint16_t i, sum = 0;

  for (i=0; i<(message->messageLength);i++) {
    sum += ((uint8_t*)message)[i];
  }
  return sum;
}

/**
 * Returns an RDM message's checksum field.
 *
 * @param message  Pointer to RDM message
 */
static uint16_t extractChecksum(RdmMessage* message) {
  uint16_t pos;

  pos = message->messageLength;
  return (((uint8_t*)message)[pos] << 8) | ((uint8_t*)message)[pos+1];
}

/**
 * Swap byte order between little endia and network byte order (big endian).
 *
 * @param message  Pointer to RDM message
 */
static void byteSwapAllElements(RdmMessage* message) {
 	uint8_t temp;

 	temp = (uint8_t)(message->parameterId);
 	message->parameterId = (message->parameterId >> 8) | (temp << 8);

 	temp = (uint8_t)(message->subDevice);
 	message->subDevice = (message->subDevice >> 8) | (temp << 8);
}

/**
 * Calculate an RDM message's checksum and set it in the checksum field.
 *
 * @param message  Pointer to RDM message
 */
static void setChecksum(RdmResponse* response) {
 	uint16_t i, sum = 0;

 	for (i=0; i<(response->messageLength);i++) {
 		sum += ((uint8_t*)response)[i];
 	}

 	((uint8_t*)response)[response->messageLength] = sum>>8;
 	((uint8_t*)response)[response->messageLength+1] = (uint8_t)sum;
}

/**
 * Check if the checksum is correct in an RDM message.
 *
 * @param message  Pointer to RDM message
 */
static bool checksumIsOk(RdmMessage* message) {
  return extractChecksum(message) == calculateChecksum(message);
}

/**
 * Check if our UID is within the search range.
 *
 * @param lower  The low end of the search range
 * @param upper  The upper end of the search range
 */
static bool myUidIsWithinRange(Uid* lower, Uid* upper) {
 	if ((my_uid.mId >= lower->mId) && (my_uid.mId <= upper->mId)) {
 		if ((my_uid.mId > lower->mId) || (my_uid.devId >= lower->devId)) {
 			if ((my_uid.mId < upper->mId) || (my_uid.devId <= upper->devId)) {
 				return true;
 			}
 		}
 	}
 	return false;
}

/**
 * Helper function to update packet length and command class for a response.
 *
 * @param r  Pointer to the RDM response
 */
uint8_t prepareResponse(RdmResponse* r) {
  r->commandClass |= 0x01;
  byteSwapAllElements((RdmMessage*)r);
  r->messageLength = r->parameterDataLength + 24;
  setChecksum(r);
  return r->messageLength + 2;
}

/**
 * Helper function to create a NACK response.
 *
 * @param request     Pointer to the RDM request
 * @param nackReason  The NACK reason from E1.20 standard
 * 
 */
void nack(RdmRequest* request, uint16_t nackReason) {
	RdmResponse* response;

	response = (RdmResponse*)request;

	response->responseType = RESPONSE_TYPE_NACK_REASON;
	response->parameterDataLength = 2;
	response->parameterData[0] = (nackReason >> 8);
	response->parameterData[1] = nackReason;
}

/**
 * Process a MUTE or UNMUTE request.
 *
 * @param request     Pointer to the RDM request
 * 
 */
static void discMuteOrUnMute(RdmRequest* request) {
	RdmResponse* response;

  /* NACK if command class is incorrect */
	if (request->commandClass != DISCOVERY_COMMAND) {
    Serial.println("MUTE: NACK CC");
		nack(request, NR_UNSUPPORTED_COMMAND_CLASS);
		return;
	}

  /* NACK if message has wrong length */
	if (request->parameterDataLength != 0) {
    Serial.println("MUTE: NACK FE");
		nack(request, NR_FORMAT_ERROR);
		return;
	}

	response = (RdmResponse*)request;

	if (request->parameterId == DISC_MUTE) {
    /* mute */
		is_muted = true;
    Serial.println("MUTE: MUTE");
	} else {
    /* unmute */
    Serial.println("MUTE: UNMUTE");
		is_muted = false;
	}

  /* prepare response */
	response->responseType = RESPONSE_TYPE_ACK;
	response->parameterDataLength = 2;
	response->parameterData[0] = 0;
	response->parameterData[1] = 0;
  Serial.println("MUTE: RESP");
}

/**
 * Process a IDENTIFY_DEVICE request.
 *
 * @param request     Pointer to the RDM request
 * 
 */
static void identifyDevice(RdmRequest* request) {
	RdmResponse* response;

	response = (RdmResponse*)request;

  /* NACK if command class is wrong */
	if ((request->commandClass != SET_COMMAND) && (request->commandClass != GET_COMMAND)){
		nack(request, NR_UNSUPPORTED_COMMAND_CLASS);
		return;
	}

	if (request->commandClass == SET_COMMAND) {
    /* set */
		if (request->parameterDataLength != 1) {
			nack(request, NR_FORMAT_ERROR);
			return;
		}
		switch (request->parameterData[0]) {
			case 0x00:
				/* identify off */
        is_identifying = false;
				Serial.println("RDM: identify off");
				break;
			case 0x01:
				/* identify on */
        is_identifying = true;
				Serial.println("RDM: identify on");
				break;
			default:
				nack(request, NR_DATA_OUT_OF_RANGE);
				return;
				break;
		}
		response->parameterDataLength = 0;
	} else {
    /* get */
		response->parameterDataLength = 1;
	}

	response->responseType = RESPONSE_TYPE_ACK;
	response->parameterData[0] = is_identifying ? 1 : 0;
}

/**
 * Process a DEVICE_INFO request.
 *
 * @param request     Pointer to the RDM request
 * 
 */
static void deviceInfo(RdmRequest* request) {
	RdmResponse* response;

  /* Only GET is allowed */
	if (request->commandClass != GET_COMMAND) {
		nack(request, NR_UNSUPPORTED_COMMAND_CLASS);
		return;
	}

	response = (RdmResponse*)request;

	response->responseType = RESPONSE_TYPE_ACK;
	response->parameterDataLength = 19;
	/* RDM version */
	response->parameterData[0] = 0x01;
	response->parameterData[1] = 0x00;
	/* device model id, this should be updated in a real world application */
	response->parameterData[2] = 0xC0;
	response->parameterData[3] = 0xDE;
	/* product category */
	response->parameterData[4] = PRODUCT_CATEGORY_FIXTURE >> 8;
	response->parameterData[5] = PRODUCT_CATEGORY_FIXTURE & 0x00FF;
	/* software version ID, this should be updated in a real world application  */
	response->parameterData[6] = 1;
	response->parameterData[7] = 0;
	response->parameterData[8] = 0;
	response->parameterData[9] = 0;
	/* footprint */
	response->parameterData[10] = dmx_footprint >> 8;
	response->parameterData[11] = dmx_footprint;
	/* personality */
	response->parameterData[12] = 1;
	response->parameterData[13] = 1;
	/* start adress */
	response->parameterData[14] = (dmx_start_addr+1) >> 8;
	response->parameterData[15] = (dmx_start_addr+1);
	/* sub device count */
	response->parameterData[16] = 0x00;
	response->parameterData[17] = 0x00;
	/* sensor count */
	response->parameterData[18] = 0x00;
}

/**
 * Process a DISCOVERY_UNIQUE_BRANCH request.
 *
 * @param request     Pointer to the RDM request
 * 
 */
static uint8_t discUniqueBranch(RdmRequest* request) {
	uint8_t* response;
	Uid upper, lower;
	uint8_t i;
	uint16_t sum = 0;

	lower.mId = mId(request->parameterData);
	lower.devId = devId(request->parameterData);
	upper.mId = mId(request->parameterData+6);
	upper.devId = devId(request->parameterData+6);

	response = (uint8_t*)request;

  /* if out UID is within range and we are not already muted we should respond */
	if ((myUidIsWithinRange(&lower, &upper)) && (!is_muted)) {
		for (i=0; i<7; i++) {
			response[i] = 0xFE;
		}
		response[7] = 0xAA;
		response[8] = (my_uid.mId >> 8) | 0xAA;
		response[9] = (my_uid.mId >> 8) | 0x55;
		response[10] = (uint8_t)(my_uid.mId) | 0xAA;
		response[11] = (uint8_t)(my_uid.mId) | 0x55;
		for (i=0; i<4; i++) {
			response[12+2*i] = ((uint8_t*)&(my_uid.devId))[3-i] | 0xAA;
			response[13+2*i] = ((uint8_t*)&(my_uid.devId))[3-i] | 0x55;
		}
		for (i=0; i<12; i++) {
			sum += response[8+i];
		}
		response[20] = (sum >> 8) | 0xAA;
		response[21] = (sum >> 8) | 0x55;
		response[22] = (uint8_t)sum | 0xAA;
		response[23] = (uint8_t)sum | 0x55;

		return 24;
	}

  return 0;
}

/**
 * Process a SOFTWARE_VERSION_LABEL request.
 *
 * @param request     Pointer to the RDM request
 * 
 */
static void softwareVersionLabel(RdmRequest* request) {
	RdmResponse* response;

	response = (RdmResponse*)request;

  /* only GET is allowed */
	if (request->commandClass != GET_COMMAND) {
		nack(request, NR_UNSUPPORTED_COMMAND_CLASS);
		return;
	}

	response->responseType = RESPONSE_TYPE_ACK;
	memcpy(response->parameterData, "1.0.0\0", 6);
	response->parameterDataLength = 6;
}

/**
 * Process a DEVICE_MODEL_DESCRIPTION request.
 *
 * @param request     Pointer to the RDM request
 * 
 */
static void deviceModelDescription(RdmRequest* request) {
	RdmResponse* response;

  /* only GET is allowed */
	if (request->commandClass != GET_COMMAND) {
		nack(request, NR_UNSUPPORTED_COMMAND_CLASS);
		return;
	}

	response = (RdmResponse*)request;

	response->responseType = RESPONSE_TYPE_ACK;
	response->parameterDataLength = 22;
	memcpy(response->parameterData, "SPI RDM Test Responder", 22);
}

/**
 * Process a MANUFACTURER_LABEL request.
 *
 * @param request     Pointer to the RDM request
 * 
 */
static void manufacturerLabel(RdmRequest* request) {
	RdmResponse* response;

  /* only GET is allowed */
	if (request->commandClass != GET_COMMAND) {
		nack(request, NR_UNSUPPORTED_COMMAND_CLASS);
		return;
	}

	response = (RdmResponse*)request;

	response->responseType = RESPONSE_TYPE_ACK;
  response->parameterDataLength = 10;
  memcpy(response->parameterData, "LumenRadio", 10);
}

/**
 * Process a SUPPORTED_PARAMETERS request.
 *
 * @param request     Pointer to the RDM request
 * 
 */
static void supportedParameters(RdmRequest* request) {
  RdmResponse* response;
  uint8_t i;

  /* only GET is allowed */
  if (request->commandClass != GET_COMMAND) {
    nack(request, NR_UNSUPPORTED_COMMAND_CLASS);
    return;
  }

  response = (RdmResponse*)request;

  response->responseType = RESPONSE_TYPE_ACK;

  for (i=0; i<N_SUPPORTED_PIDS; i++) {
    response->parameterData[2*i] = (uint8_t)(supportedPidList[i] >> 8);
    response->parameterData[2*i+1] = (uint8_t)supportedPidList[i];
  }
  response->parameterDataLength = 2*N_SUPPORTED_PIDS;
}

/**
 * Process a DEVICE_LABEL request.
 *
 * @param request     Pointer to the RDM request
 * 
 */
static void deviceLabel(RdmRequest* request) {
  RdmResponse* response;

  if (request->commandClass == GET_COMMAND) {
    /* if it's a GET, return the current label */
    response = (RdmResponse*)request;

    response->responseType = RESPONSE_TYPE_ACK;
    response->parameterDataLength = device_label_length;

    memcpy(response->parameterData, device_label, device_label_length);
  } else if (request->commandClass == SET_COMMAND) {
    /* if it's a SET, update the label */
    response = (RdmResponse*)request;

    device_label_length = MIN(response->parameterDataLength, 32);
    memcpy(device_label, response->parameterData, device_label_length);
    
    response->responseType = RESPONSE_TYPE_ACK;
    response->parameterDataLength = 0;

  } else {
    /* no other command classes allowed */
    nack(request, NR_UNSUPPORTED_COMMAND_CLASS);
  }

  return;
}

/**
 * Process a DMX_START_ADDRESS request.
 *
 * @param request     Pointer to the RDM request
 * 
 */
static void dmxStartAddress(RdmRequest* request) {
  RdmResponse* response;
  uint16_t addr;

  if (request->commandClass == GET_COMMAND) {
    /* return the current DMX address */
    response = (RdmResponse*)request;

    response->responseType = RESPONSE_TYPE_ACK;
    response->parameterDataLength = 2;

    addr = dmx_start_addr + 1;
    response->parameterData[0] = addr >> 8;
    response->parameterData[1] = addr;
  } else if (request->commandClass == SET_COMMAND) {
    /* update the DMX address if it's a SET */
    if (request->parameterDataLength != 2) {
      nack(request, NR_FORMAT_ERROR);
    } else {
      dmx_start_addr = ((uint16_t)request->parameterData[0] << 8) | (uint16_t)request->parameterData[1];
      dmx_start_addr -= 1;
      should_update_dmx_window = true;
      
      response = (RdmResponse*)request;
    
      response->responseType = RESPONSE_TYPE_ACK;
      response->parameterDataLength = 0;
    }
  } else {
    /* No other command classes allowed */
    nack(request, NR_UNSUPPORTED_COMMAND_CLASS);
  }

  return;
}

/**
 * Helper function that swaps destination and source addresses in a RDM message buffer.
 *
 * @param request     Pointer to the RDM message buffer
 * 
 */
static void swapSrcDest(RdmMessage* m) {
  uint8_t temp[6];

  memcpy(temp, m->destinationUid, 6);
  memcpy(m->destinationUid, m->sourceUid, 6);
  memcpy(m->sourceUid, temp, 6);
}

/**
 * Process the RDM request.
 *
 * @param request     Pointer to the RDM message buffer
 * 
 */
static uint8_t parseRequest(RdmRequest* request) {
  Uid destination;
  RdmResponse* response = (RdmResponse*)request;

  /* check checksum */
  if (!checksumIsOk((RdmMessage*)request)) {
    Serial.println("RDM: invalid checksum");
    /* don't continue oif checksum is invalid */
    return 0;
  }

  destination.mId = mId(request->destinationUid);
	destination.devId = devId(request->destinationUid);

  /* process it it's to us, or if it's broadcast, or vendorcast to our manufacturer ID */
  if (isToMe(&destination) || (isBroadcast(&destination) && ((destination.mId == 0xFFFF) || (destination.mId == my_uid.mId)))) {
    Serial.println("RDM: request is to me");

    byteSwapAllElements((RdmMessage*)request);

  	if (request->commandClass == DISCOVERY_COMMAND) {
  		if ((request->parameterId != DISC_UNIQUE_BRANCH) && (request->parameterId != DISC_MUTE) && (request->parameterId != DISC_UN_MUTE)) {
  			return 0;
  		}
  	}

    /* this responder do not have any subdevices - if it's addressed to a subdevice we need to NACK this */
  	if (request->subDevice != 0) {
      nack(request, NR_SUB_DEVICE_OUT_OF_RANGE);
  		return prepareResponse(response);
  	}

    /* check what PID to process */
  	switch (request->parameterId) {
  		case DISC_UNIQUE_BRANCH:
        Serial.println("DISC_UNIQUE_BRANCH");
  			return discUniqueBranch(request);
  			break;
  		case DISC_MUTE:
        Serial.println("DISC_MUTE");
        discMuteOrUnMute(request);
        break;
  		case DISC_UN_MUTE:
        Serial.println("DISC_UN_MUTE");
  			discMuteOrUnMute(request);
  			break;
      case IDENTIFY_DEVICE:
        Serial.println("IDENTIFY_DEVICE");
  			identifyDevice(request);
  			break;
  		case DEVICE_INFO:
        Serial.println("DEVICE_INFO");
  			deviceInfo(request);
  			break;
  		case SOFTWARE_VERSION_LABEL:
        Serial.println("SOFTWARE_VERSION_LABEL");
  			softwareVersionLabel(request);
  			break;
  		case MANUFACTURER_LABEL:
        Serial.println("MANUFACTURER_LABEL");
  			manufacturerLabel(request);
  			break;
  		case DEVICE_LABEL:
        Serial.println("DEVICE_LABEL");
  			deviceLabel(request);
  			break;
  		case DEVICE_MODEL_DESCRIPTION:
        Serial.println("DEVICE_MODEL_DESCRIPTION");
  			deviceModelDescription(request);
  			break;
  		case SUPPORTED_PARAMETERS:
        Serial.println("SUPPORTED_PARAMETERS");
  			supportedParameters(request);
  			break;
      case DMX_START_ADDRESS:
        Serial.println("DMX_START_ADDRESS");
        dmxStartAddress(request);
        break;
  		default:
        Serial.println("UNKOWN_PID");
  			nack(request, NR_UNKNOWN_PID);
  			break;
  	}
    if (isBroadcast(&destination)) {
      return 0;
    } else {
      swapSrcDest((RdmMessage*)response);
      return prepareResponse(response);
    }
  } else {
    Serial.println("RDM: request is not to me");
    return 0;
  }
}
