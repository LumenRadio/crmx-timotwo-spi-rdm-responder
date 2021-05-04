/*
 * e120.h
 * Description:
 */

#ifndef E120_H_
#define E120_H_

/* Attributes */
#ifndef PACKED
#define PACKED			__attribute__((__packed__))
#endif
#define NO_INLINE		__attribute__((noinline))
#define ALWAYS_INLINE	__attribute__((always_inline))
#define NO_RETURN		__attribute__((noreturn))
#define ALIGNED			__attribute__ ((aligned (4)))

typedef struct ALIGNED {
	uint16_t mId;
	uint32_t devId;
} Uid;

#define RDM_PROTOCOL_VERSION		0x0100

#define SC_RDM						0xCC
#define SC_SUB_MESSAGE		0x01

/* Table A-1: Command Class Defines */
#define DISCOVERY_COMMAND 			    0x10
#define DISCOVERY_COMMAND_RESPONSE	0x11
#define GET_COMMAND 				        0x20
#define GET_COMMAND_RESPONSE 		    0x21
#define SET_COMMAND 				        0x30
#define SET_COMMAND_RESPONSE 		    0x31

/* Table A-2: Response Type Defines */
#define RESPONSE_TYPE_ACK			      0x00
#define RESPONSE_TYPE_ACK_TIMER		  0x01
#define RESPONSE_TYPE_NACK_REASON	  0x02
#define RESPONSE_TYPE_ACK_OVERFLOW	0x03

/* Table A-3: RDM Parameter ID Defines */
#define DISC_UNIQUE_BRANCH					0x0001
#define DISC_MUTE							      0x0002
#define DISC_UN_MUTE						    0x0003
#define PROXIED_DEVICES						  0x0010
#define PROXIED_DEVICE_COUNT				0x0011
#define	QUEUED_MESSAGE						  0x0020
#define	STATUS_MESSAGES						  0x0030
#define SUPPORTED_PARAMETERS				0x0050
#define PARAMETER_DESCRIPTION				0x0051
#define DEVICE_INFO							    0x0060
#define PRODUCT_DETAIL_ID_LIST			0x0070
#define DEVICE_MODEL_DESCRIPTION		0x0080
#define MANUFACTURER_LABEL					0x0081
#define DEVICE_LABEL						    0x0082
#define SOFTWARE_VERSION_LABEL			0x00C0
#define BOOT_SOFTWARE_VERSION_ID		0x00C1
#define BOOT_SOFTWARE_VERSION_LABEL	0x00C2
#define DMX_PERSONALITY						  0x00E0
#define DMX_PERSONALITY_DESCRIPTION	0x00E1
#define DMX_START_ADDRESS           0x00F0
#define IDENTIFY_DEVICE						  0x1000
#define RESET_DEVICE						    0x1001
#define SENSOR_DEFINITION					  0x0200
#define SENSOR_VALUE						    0x0201

/* Table A-4: Status Type Defines */
#define STATUS_NONE					      0x00
#define STATUS_GET_LAST_MESSAGE 	0x01
#define STATUS_ADVISORY				    0x02
#define STATUS_WARNING				    0x03
#define STATUS_ERROR				      0x04

/* Table A-12: Sensor types */
#define SENS_OTHER					0x7F

/* Table A-13: Units */
#define UNITS_NONE					0x00
#define UNITS_SECOND				0x15

/* Table A-14: Prefixes */
#define PREFIX_NONE					0x00
#define PREFIX_DECI					0x01


/* Table A-15: Data types */
#define DS_NOT_DEFINED 				0x00
#define DS_BIT_FIELD				  0x01
#define DS_ASCII					    0x02
#define DS_UNSIGNED_UINT8			0x03
#define DS_SIGNED_UINT8				0x04
#define DS_UNSIGNED_WORD			0x05
#define DS_SIGNED_WORD				0x06
#define DS_UNSIGNED_DWORD			0x07
#define DS_SIGNED_DWORD				0x08

/* Table: A-16: Command classes */
#define CC_GET				0x01
#define CC_SET				0x02
#define CC_GET_SET	  0x03

/* Table A-17: Response NACK Reason Code Defines */
#define NR_UNKNOWN_PID					      0x0000
#define NR_FORMAT_ERROR					      0x0001
#define NR_HARDWARE_FAULT				      0x0002
#define NR_PROXY_REJECT				       	0x0003
#define NR_WRITE_PROTECT				      0x0004
#define NR_UNSUPPORTED_COMMAND_CLASS	0x0005
#define NR_DATA_OUT_OF_RANGE			    0x0006
#define NR_BUFFER_FULL					      0x0007
#define NR_PACKET_SIZE_UNSUPPORTED		0x0008
#define NR_SUB_DEVICE_OUT_OF_RANGE		0x0009
#define NR_PROXY_BUFFER_FULL			    0x000A


#define PRODUCT_CATEGORY_FIXTURE    0x0100

typedef struct PACKED ALIGNED {
	uint8_t	startCode;
	uint8_t	subStartCode;
	uint8_t	messageLength;
	uint8_t	destinationUid[6];
	uint8_t	sourceUid[6];
	uint8_t	transactionNumber;
	uint8_t	portId;
	uint8_t	messageCount;
	uint16_t subDevice;
	uint8_t	commandClass;
	uint16_t parameterId;
	uint8_t	parameterDataLength;
	uint8_t parameterData[230];
} RdmRequest;

typedef struct PACKED ALIGNED {
	uint8_t	startCode;
	uint8_t	subStartCode;
	uint8_t	messageLength;
	uint8_t	destinationUid[6];
	uint8_t	sourceUid[6];
	uint8_t	transactionNumber;
	uint8_t	responseType;
	uint8_t	messageCount;
	uint16_t subDevice;
	uint8_t	commandClass;
	uint16_t parameterId;
	uint8_t	parameterDataLength;
	uint8_t parameterData[230];
} RdmResponse;

typedef struct PACKED ALIGNED {
	uint8_t	startCode;
	uint8_t	subStartCode;
	uint8_t	messageLength;
	uint8_t	destinationUid[6];
	uint8_t	sourceUid[6];
	uint8_t	transactionNumber;
	uint8_t	portIdOrResponseType;
	uint8_t	messageCount;
	uint16_t subDevice;
	uint8_t	commandClass;
	uint16_t parameterId;
	uint8_t	parameterDataLength;
	uint8_t parameterData[230];
} RdmMessage;




#endif /* E120_H_ */
