/**
 * @file MQTTSN.c
 * @author dyl
 * @brief 
 * @version 0.1
 * @date 2022-12-04
 * 
 * @copyright Copyright (c) 2022
 * 
 * MQTTSN�ͻ��ˣ�md����һ���ļ��õ�vcԶ�����ӵ�Linux�����������ֲ��Ƕ��ʽ�����С�
 * �󲿷ִ��뻹�� �ٷ���packet����ֻ�������˲ü������ڹٷ����ĵ����ǲ�̫��⡣
 * �㹻���Ļ�����ʵ�ʵ�ȥ�޸Ĳ�ͬ�ĺ�������
 */

/*ͷ�ļ�λ��*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
/*�궨��λ��*/
#define SOCKET_ERROR -1                          //�׽��ֱ���
#define INVALID_SOCKET SOCKET_ERROR
#define MQTTSNPacket_connectData_initializer { {'M', 'Q', 'S', 'C'}, 0, {NULL, {0, NULL}}, 10, 1, 0 } //����ʱ��ĳ�ʼ��

/*ȫ�ֱ���λ��*/
typedef enum
{
	MQTTSN_TOPIC_TYPE_NORMAL, /* topic id in publish, topic name in subscribe */
	MQTTSN_TOPIC_TYPE_PREDEFINED,
	MQTTSN_TOPIC_TYPE_SHORT
}MQTTSN_topicTypes;


enum MQTTSN_msgTypes
{
    /*MQTTSN��Ϣ������*/
	MQTTSN_ADVERTISE, MQTTSN_SEARCHGW, MQTTSN_GWINFO, MQTTSN_RESERVED1,
	MQTTSN_CONNECT, MQTTSN_CONNACK,
	MQTTSN_WILLTOPICREQ, MQTTSN_WILLTOPIC, MQTTSN_WILLMSGREQ, MQTTSN_WILLMSG, 
	MQTTSN_REGISTER, MQTTSN_REGACK,
	MQTTSN_PUBLISH, MQTTSN_PUBACK, MQTTSN_PUBCOMP, MQTTSN_PUBREC, MQTTSN_PUBREL, MQTTSN_RESERVED2,
	MQTTSN_SUBSCRIBE, MQTTSN_SUBACK, MQTTSN_UNSUBSCRIBE, MQTTSN_UNSUBACK, 
	MQTTSN_PINGREQ, MQTTSN_PINGRESP,
	MQTTSN_DISCONNECT, MQTTSN_RESERVED3, 
	MQTTSN_WILLTOPICUPD, MQTTSN_WILLTOPICRESP, MQTTSN_WILLMSGUPD, MQTTSN_WILLMSGRESP,
	MQTTSN_ENCAPSULATED = 0xfe
};

enum errors
{
    /*����λ�ã���ʱ�������ɡ�*/
	MQTTSNPACKET_BUFFER_TOO_SHORT = -2,
	MQTTSNPACKET_READ_ERROR = -1,
	MQTTSNPACKET_READ_COMPLETE
};

/*��̬ȫԱ����λ��*/
static int mysock = INVALID_SOCKET; //�������sock����ġ�

/*�������Ͷ���*/
typedef struct
{
	MQTTSN_topicTypes type;
	union
	{
		unsigned short id;
		char short_name[2];
		struct
		{
			char* name;
			int len;
		} long_;
	} data;
} MQTTSN_topicid;

typedef struct
{
	int len;
	char* data;
} MQTTSNLenString;

typedef struct
{
	char* cstring;
	MQTTSNLenString lenstring;
} MQTTSNString;

typedef struct
{
	
	char struct_id[4];  /** ������MQSC�����ˣ��̶� */
	int struct_version; 	/** �ṹ�ĵİ汾�ţ�������0��Ҫ���ǹٷ��ĳ�ʼ�汾��Ŀǰ�����õľ��ǳ�ʼ�汾��*/
	MQTTSNString clientID;  /*�ͻ��˵�ID��ģ���c+��string*/
	unsigned short duration; /*����ʱ���ֵ��ʱ���������ôŪ�������*/
	unsigned char cleansession; /*��MQTT�ĺ�����ͬ������Will�����Will��Ϣ��������չ*/
	unsigned char willFlag;     /*������ã������ͻ�����������Will�����Will��Ϣ��ʾ��will��Ϣ��ʱ���������ʹ�á�*/
} MQTTSNPacket_connectData;

typedef union
{
    /*�������������Ҫ��Ϊ�˸�MQTTSNPacket_connectData����������������all������ﲻ֪������Ϣ������д�ˣ��ֺ���ûд����*/
	unsigned char all;
	struct
	{
		unsigned int topicIdType : 2;
		unsigned int cleanSession : 1;
		unsigned int will : 1;
		unsigned int retain : 1;
		unsigned int QoS : 2;
		int dup: 1;
	} bits;
} MQTTSNFlags;

/*****************************************************
 *       dyl 
 * 
 *       ��������λ��
 * 
 ******************************************************/

int MQTTSNSerialize_connect(unsigned char* buf, int buflen, MQTTSNPacket_connectData* options);
int MQTTSNPacket_len(int length);
int MQTTSNSerialize_connectLength(MQTTSNPacket_connectData* options);
int MQTTSNstrlen(MQTTSNString MQTTSNString);
void writeChar(unsigned char** pptr, char c);
void writeInt(unsigned char** pptr, int anInt);
void writeMQTTSNString(unsigned char** pptr, MQTTSNString MQTTSNString);
void writeCString(unsigned char** pptr, char* string);
int MQTTSNPacket_encode(unsigned char* buf, int length);
int MQTTSNPacket_read(unsigned char* buf, int buflen, int (*getfn)(unsigned char*, int));
int MQTTSNPacket_decode(unsigned char* buf, int buflen, int* value);
int readInt(unsigned char** pptr);
int transport_getdata(unsigned char* buf, int count);
int transport_sendPacketBuffer(char* host, int port, unsigned char* buf, int buflen);
int Socket_error(char* aString, int sock);
int transport_open();
int transport_close();
/*����Ϊֹ�����Ϻ������ǰ������ӵ�ʱ���ʹ���Ǹ����͵��Ǹ�*/

/****************************************************
 *            dyl
 * 
 *                      ������
 * 
 * 
 ****************************************************/

int main()
{
    MQTTSN_topicid topic;      //
    MQTTSNString topicstd;    //  ������ʱ�ò���
    unsigned char buf[512];	  //  ��������
    int buflen = sizeof(buf);
    int len = 0;			  
    int retained = 0;         //��ʱ�ò�����
    char *topicname = "MyTopicName";
    char *ClientID  = "dyl";				//id����Ψһ��
    char *host      = "192.168.2.163";      //��������ַû�Թ�������¼��
    int port = 1884;						//�˿ں� mqttsn��Ϊ1884
    unsigned char message[512] = "Message";	//��Ϣ��push֮���á�
    /*��ʼ��*/
    MQTTSNPacket_connectData options = MQTTSNPacket_connectData_initializer;
    /*������Ϣ���߶��ĵ�ID����Ҫӳ��*/
    unsigned short TopicID = 1;
    printf("--------------------------\n");
    /* ************************************************/
    /*�������һ��socket�ı�ʶ����mysock��һ����̬��ȫ�ֱ���*/
    mysock = transport_open(); 

    /*   ****************************************** */
    options.clientID.cstring = ClientID;
    /*��������Ҫ��������װbuff��������Ϣ*/
    len = MQTTSNSerialize_connect(buf,buflen,&options);
    printf("%d\n",len);    //ʧ�ܷ���

    // for(int i=0;i<len;i++) printf("%d ",buf[i]);
    // printf("\n------------------------------------\n");
    int sn = transport_sendPacketBuffer(host,port,buf,len);
    // printf("sn1 == %d\n",sn);
    // sn = MQTTSNPacket_read((unsigned char *)buf,buflen,transport_getdata);
    // printf("sn2 == %d\n",sn);
    // if(sn==MQTTSN_CONNACK)
    // {
    //     printf("����connack\n");
    // }
    if(MQTTSNPacket_read(buf,buflen,transport_getdata)==MQTTSN_CONNACK)
    {
        for(int i=0;i<buflen;i++) printf("%d ",buf[i]);
		char *buff = "\n����connack\n";
		int  datalen = 0;
		printf("%s",buff);
    }
}


/**************************************************************************
*
*     ����ʵ��λ��
*
*
***************************************************************************/



/**
 * ����MQTTSNString-C�ַ����ĳ��ȣ�����еĻ���������ǳ����޶����ַ�����
 * @param MQTTSNString Ҫ���س��ȵ��ַ���
 * @return the length of the string
 */
int MQTTSNstrlen(MQTTSNString MQTTSNString)
{
	int rc = 0;

	if (MQTTSNString.cstring)
		rc = strlen(MQTTSNString.cstring);
	else
		rc = MQTTSNString.lenstring.len;
	return rc;
}

/**
  * ȷ��MQTT�������ݰ��ĳ��ȣ������ݰ���ʹ�����ṩ������ѡ�������
  * @param options ���������������ݰ���ѡ�
  * @return �������ݰ������л��汾����Ļ������ĳ���
  */
int MQTTSNSerialize_connectLength(MQTTSNPacket_connectData* options)
{
	int len = 0;
	len = 5 + MQTTSNstrlen(options->clientID);
	return len;
}


/**
  *������ѡ�����л�����������
  * @param buf ���ݰ��������л��Ļ�������
  * @param len ���ṩ�Ļ������ĳ��ȣ��ֽڣ���
  * @param options ���������������ݰ���ѡ��
  * @�������л��ĳ��ȣ����Ϊ0����Ϊ����
  */
int MQTTSNSerialize_connect(unsigned char* buf, int buflen, MQTTSNPacket_connectData* options)
{
	unsigned char *ptr = buf;
	MQTTSNFlags flags;
	int len = 0;
	int rc = -1;

	if ((len = MQTTSNPacket_len(MQTTSNSerialize_connectLength(options))) > buflen)
	{
		rc = MQTTSNPACKET_BUFFER_TOO_SHORT;
		goto exit;
	}
	ptr += MQTTSNPacket_encode(ptr, len); /* write length */
	writeChar(&ptr, MQTTSN_CONNECT);      /* write message type */

	flags.all = 0;
	flags.bits.cleanSession = options->cleansession;
	flags.bits.will = options->willFlag;
	writeChar(&ptr, flags.all);
	writeChar(&ptr, 0x01);                 /* protocol ID */
	writeInt(&ptr, options->duration);
	writeMQTTSNString(&ptr, options->clientID);
	rc = ptr - buf;
exit:
    printf("rc connect1 %d\n",rc);
	return rc;
}


void writeMQTTSNString(unsigned char** pptr, MQTTSNString MQTTSNString)
{
	if (MQTTSNString.lenstring.len > 0)
	{
		memcpy(*pptr, (const unsigned char*)MQTTSNString.lenstring.data, MQTTSNString.lenstring.len);
		*pptr += MQTTSNString.lenstring.len;
	}
	else if (MQTTSNString.cstring)
		writeCString(pptr, MQTTSNString.cstring);
}


/**
 *��һ��������Ϊ2���ֽ�д�������������
 * @param pptr ָ�������������ָ�� - ��ʹ�õ��ֽ������������ء�
 * @param anInt Ҫд���������0��65535
 */
void writeInt(unsigned char** pptr, int anInt)
{
	**pptr = (unsigned char)(anInt / 256);
	(*pptr)++;
	**pptr = (unsigned char)(anInt % 256);
	(*pptr)++;
}

/**
 * дһ���ַ���һ�������������
 * @param pptr ָ�������������ָ�� - ��ʹ�õ��ֽ������������ء�
 * @param c Ҫд���ַ�
 */
void writeChar(unsigned char** pptr, char c)
{
	**pptr = (unsigned char)c;
	(*pptr)++;
}

/**
 * дһ�� "UTF "�ַ���������������� ��C�����ַ���ת��Ϊ�Գ���Ϊ���޵��ַ�����
 * @param pptr ָ�������������ָ�� - ��ʹ�úͷ��ص��ֽ���������
 * @param string Ҫд���C�����ַ���
 */
void writeCString(unsigned char** pptr, char* string)
{
	int len = strlen(string);
	memcpy(*pptr, string, len);
	*pptr += len;
}

/**
 *��MQTT-SN��Ϣ�ĳ��Ƚ��б���
 * @param buf �����������������ݱ�д������
 * @param length ��Ҫ����ĳ���
 * @return д�뻺�������ֽ���
 */
int MQTTSNPacket_encode(unsigned char* buf, int length)
{
	int rc = 0;

	if (length >= 255)
	{
		writeChar(&buf, 0x01);
		writeInt(&buf, length);
		rc += 3;
	}
	else
		buf[rc++] = length;

	return rc;
}

/**
 * ������������ֶ����ڵ�ȫ�����ݰ�����
 *@param length ���������ֶε�MQTT-SN���ݰ��ĳ���
 *@return ��MQTT-SN���ݰ����ܳ��ȣ����������ֶΡ�
 */
int MQTTSNPacket_len(int length)
{
	return (length >= 255) ? length + 3 : length + 1;
}


/**
 * �����ݰ���ĳ��Դͷ����һ���������ĸ�������
 * @param buf �����������ݰ��������л������С�
 * @param buflen ���ṩ�Ļ������ĳ��ȣ��ֽڣ���
 * @param getfn ָ��һ��������ָ�룬�ú���������Ҫ����Դ��ȡ�κ��������ֽڡ�
 * @return integer MQTT packet type, or MQTTSNPACKET_READ_ERROR on error
 */
int MQTTSNPacket_read(unsigned char* buf, int buflen, int (*getfn)(unsigned char*, int))
{
	int rc = MQTTSNPACKET_READ_ERROR;
	const int MQTTSN_MIN_PACKET_LENGTH = 2;
	int len = 0;  /* the length of the whole packet including length field */
	int lenlen = 0;
	int datalen = 0;

	/* 1. read a packet - UDP style */
	if ((len = (*getfn)(buf, buflen)) < MQTTSN_MIN_PACKET_LENGTH)
        // printf("len1 %d\n",len);
		goto exit;
    // printf("rc1 %d\n",rc);
	/* 2. read the length.  This is variable in itself */
	lenlen = MQTTSNPacket_decode(buf, len, &datalen);
	if (datalen != len)
		goto exit; /* there was an error */
    // printf("rc2 %d\n",rc);
	rc = buf[lenlen]; /* �������ؽ��������� */
exit:
    printf("rc3 %d lenlen = %d\n",rc,lenlen);
	return rc;
}


/**
 *���յ��������л�ȡMQTT-SN���ݰ��ĳ���
 * @param buf�ǰ���MQTT-SN���ݰ��Ļ�������
 * @param buflen ���ṩ�Ļ������ĳ��ȣ��ֽڣ���
 * @param value ���صĽ��볤��
 * @return ���׽����ж�ȡ���ֽ���
 */
int MQTTSNPacket_decode(unsigned char* buf, int buflen, int* value)
{
	int len = MQTTSNPACKET_READ_ERROR;
#define MAX_NO_OF_LENGTH_BYTES 3

	if (buflen <= 0)
		goto exit;

	if (buf[0] == 1)
	{
		unsigned char* bufptr = &buf[1];
		if (buflen < MAX_NO_OF_LENGTH_BYTES)
			goto exit;
		*value = readInt(&bufptr);
		len = 3;
	}
	else
	{
		*value = buf[0];
		len = 1;
	}
exit:
	return len;
}


/**
 *�����뻺���������������ֽ��м����һ������
 * @param pptr ָ�����뻺������ָ�� - ��ʹ�úͷ��ص��ֽ�������
 * @return �����������ֵ
 */
int readInt(unsigned char** pptr)
{
	unsigned char* ptr = *pptr;
	int len = 256*((unsigned char)(*ptr)) + (unsigned char)(*(ptr+1));
	*pptr += 2;
	return len;
}

int transport_getdata(unsigned char* buf, int count)
{

	int rc = recvfrom(mysock, (char *)buf, count, 0, NULL, NULL);
	printf("received %d bytes count %d\n", rc, (int)count);
	return rc;
}

int transport_sendPacketBuffer(char* host, int port, unsigned char* buf, int buflen)
{
	struct sockaddr_in cliaddr;
	int rc = 0;

	memset(&cliaddr, 0, sizeof(cliaddr));
	cliaddr.sin_family = AF_INET;
	cliaddr.sin_addr.s_addr = inet_addr(host);
	cliaddr.sin_port = htons(port);

	if ((rc = sendto(mysock, (char *)buf, buflen, 0, (const struct sockaddr*)&cliaddr, sizeof(cliaddr))) == SOCKET_ERROR)
		Socket_error("sendto", mysock);
	else
		rc = 0;
	return rc;
}


int Socket_error(char* aString, int sock)
{
	if (errno != EINTR && errno != EAGAIN && errno != EINPROGRESS && errno != EWOULDBLOCK)
	{
		if (strcmp(aString, "shutdown") != 0 || (errno != ENOTCONN && errno != ECONNRESET))
		{
			int orig_errno = errno;
			char* errmsg = strerror(errno);

			printf("Socket error %d (%s) in %s for socket %d\n", orig_errno, errmsg, aString, sock);
		}
	}
	return errno;
}

int transport_open()
{
	mysock = socket(AF_INET, SOCK_DGRAM, 0);
	if (mysock == INVALID_SOCKET)
		return Socket_error("socket", mysock);

	return mysock;
}

int transport_close()
{
	int rc;
	rc = shutdown(mysock, SHUT_WR);
	rc = close(mysock);

	return rc;
}