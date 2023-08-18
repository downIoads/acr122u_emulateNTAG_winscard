#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winscard.h>

#define IOCTL_CCID_ESCAPE SCARD_CTL_CODE(3500)		// use this if code should run without tag present

typedef struct _SCARD_DUAL_HANDLE {
	SCARDCONTEXT hContext;
	SCARDHANDLE hCard;
} SCARD_DUAL_HANDLE, * PSCARD_DUAL_HANDLE;


void PrintHex(LPCBYTE pbData, DWORD cbData)
{
	DWORD i;
	for (i = 0; i < cbData; i++)
	{
		wprintf(L"%02x ", pbData[i]);
	}
	wprintf(L"\n");
}

BOOL SendRecvReader(PSCARD_DUAL_HANDLE pHandle, const BYTE* pbData, const UINT16 cbData, BYTE* pbResult, UINT16* pcbResult)
{
	BOOL status = FALSE;
	DWORD cbRecvLenght = *pcbResult;
	LONG scStatus;

	wprintf(L"> ");
	PrintHex(pbData, cbData);

	// scStatus = SCardTransmit(pHandle->hCard, NULL, pbData, cbData, NULL, pbResult, &cbRecvLenght);
	scStatus = SCardControl(pHandle->hCard, IOCTL_CCID_ESCAPE, pbData, cbData, pbResult, cbRecvLenght, &cbRecvLenght);	// use this if code should run without tag present

	if (scStatus == SCARD_S_SUCCESS)
	{
		*pcbResult = (UINT16)cbRecvLenght;

		wprintf(L"< ");
		PrintHex(pbResult, *pcbResult);

		status = TRUE;
	}
	else wprintf(L"%08x\n", scStatus);

	return status;
}

BOOL OpenReader(LPCWSTR szReaderName, PSCARD_DUAL_HANDLE pHandle)
{
	BOOL status = FALSE;
	LONG scStatus;
	DWORD dwActiveProtocol;

	scStatus = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &pHandle->hContext);
	if (scStatus == SCARD_S_SUCCESS)
	{
		// scStatus = SCardConnect(pHandle->hContext, szReaderName, SCARD_SHARE_SHARED, SCARD_PROTOCOL_Tx, &pHandle->hCard, &dwActiveProtocol);
		scStatus = SCardConnect(pHandle->hContext, szReaderName, SCARD_SHARE_DIRECT, SCARD_PROTOCOL_UNDEFINED, &pHandle->hCard, &dwActiveProtocol); // use this if code should run without tag present
		
		if (scStatus == SCARD_S_SUCCESS)
		{
			status = TRUE;
		}
		else
		{
			SCardReleaseContext(pHandle->hContext);
		}
	}

	return status;
}

void CloseReader(PSCARD_DUAL_HANDLE pHandle)
{
	SCardDisconnect(pHandle->hCard, SCARD_LEAVE_CARD);
	SCardReleaseContext(pHandle->hContext);
}


int EmulateNTag() {

	SCARD_DUAL_HANDLE hDual;
	BYTE Buffer[512];	// TODO figure out smallest value that works
	UINT16 cbBuffer;	// will be updated depending on expected response length in bytes
	
	// my firmware: ACR122U216 (216 is firmware)

	// my Laptop:	"ACS ACR122U PICC Interface 0"
	// my PC:		"ACS ACR122 0"
	if (OpenReader(L"ACS ACR122 0", &hDual))
	{

		// to send commands directly to PN532 inside ACR122U you must use "ff 00 00 00 <lengthOfPayload> <payload>"

		// 1. read register (returns 7 byte: D5 07 xx yy zz 90 00 )
		const BYTE APDU_Command1[] = { 0xff, 0x00, 0x00, 0x00, 0x08, 0xd4, 0x06, 0x63, 0x05, 0x63, 0x0d, 0x63, 0x38 };

		cbBuffer = 7;	// 7 byte reply expected
		SendRecvReader(&hDual, APDU_Command1, sizeof(APDU_Command1), Buffer, &cbBuffer);

		// make sure this operation was successful, terminate if not
		if (!(Buffer[5] == 0x90 && Buffer[6] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			return 1;
		}
		else {
			wprintf(L"Register has successfully been read.\n\n");
		}

		// 2. update register values
		BYTE XX = Buffer[2] | 0x04;		// CIU_TxAuto		|= InitialRFOn
		BYTE YY = Buffer[3] & 0xef;		// CIU_ManualRCV	&= ~ParityDisable
		BYTE ZZ = Buffer[4] & 0xf7;		// CIU_Status2		&= ~MFCrypto1On

		// 3. write new register values
		const BYTE APDU_Command3[] = { 0xff, 0x00, 0x00, 0x00, 0x11, 0xd4, 0x08, 0x63, 0x02, 0x80, 0x63, 0x03, 0x80, 0x63, 0x05, XX, 0x63, 0x0d, YY, 0x63, 0x38, ZZ };
		cbBuffer = 4;	// 4 byte reply expected
		
		SendRecvReader(&hDual, APDU_Command3, sizeof(APDU_Command3), Buffer, &cbBuffer);
		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0xd5 && Buffer[1] == 0x09 && Buffer[2] == 0x90 && Buffer[3] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			return 1;
		}
		else {
			wprintf(L"Register has successfully been written to.\n\n");
		}

		// 4. set parameters
		const BYTE APDU_Command4[] = { 0xff, 0x00, 0x00, 0x00, 0x03, 0xd4, 0x12, 0x30 };
		cbBuffer = 4;	// 4 byte reply expected
		
		SendRecvReader(&hDual, APDU_Command4, sizeof(APDU_Command4), Buffer, &cbBuffer);
		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0xd5 && Buffer[1] == 0x13 && Buffer[2] == 0x90 && Buffer[3] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			return 1;
		}
		else {
			wprintf(L"Parameters have successfully been set.\n\n");
		}

		// 5. TgInitAsTarget (page 50 vs page 57 of PN532 Application Note AN133910 AN10449_1)
																		// mode	04 = ISO14443-4A													
		const BYTE APDU_Command5[] = { 0xff, 0x00, 0x00, 0x00, 0x26, 0x8c, 0x04, 0x08, 0x00, 0x12, 0x34, 0x56, 0x60, 0x01, 0xFE, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xff, 0xff, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00 };
	
		cbBuffer = 8;	// 8 byte reply expected
		
		while (true) {	// usually works on second try
			SendRecvReader(&hDual, APDU_Command5, sizeof(APDU_Command5), Buffer, &cbBuffer);
			if (!(Buffer[2] == 0x00)) {
				wprintf(L"Failed entering emulation mode. Retrying..\n");
			}
			else {
				wprintf(L"Successfully initiated emulation mode.\n\n");
				break;
			}
		}

		// 6. TgGetData
		const BYTE APDU_Command6[] = { 0xff, 0x00, 0x00, 0x00, 0x02, 0xd4, 0x86 };
		// const BYTE APDU_Command6[] = { 0xff, 0x00, 0x00, 0x00, 0x01, 0x86 };

		cbBuffer = 15;	// ? byte reply expected
		SendRecvReader(&hDual, APDU_Command6, sizeof(APDU_Command6), Buffer, &cbBuffer);

		// make sure this operation was successful, terminate if not
		if (!( Buffer[2] == 0x00 )) {
			CloseReader(&hDual);
			wprintf(L"Error: Did not receive 00 as third byte..\n");
			wprintf(L"Info: You must have the ACR122U drivers installed to get past this step.. Default win drivers dont work.\n");
			return 1;
		}
		else {
			wprintf(L"TgGetData has been sent.\n\n");
		}

		// reply: d5 87 25 90 00
		// PROBLEM: "25" means "Error: DEP Protocol: Invalid device state, the system is in a state which does not allow the operation"

		// -----------------------------
		// 7. TgSetData?													   |replace below with response|
		const BYTE APDU_Command7[] = { 0xff, 0x00, 0x00, 0x00, YY, 0xd4, 0x8e, 0xd5, 0x87, 0x25, 0x90, 0x00  };
		cbBuffer = 15;	// 4 byte reply expected
		
		SendRecvReader(&hDual, APDU_Command7, sizeof(APDU_Command7), Buffer, &cbBuffer);	// execute command
		// make sure this operation was successful, terminate if not
		if (!(Buffer[3] == 0x90 && Buffer[4] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			return 1;
		}
		else {
			wprintf(L"TgSetData has been sent.\n");
			wprintf(L"\nAll commands have been sent successfully.\n");
		}


		// ---------------------------
		CloseReader(&hDual);
	}
	else {
		wprintf(L"Failed to find NFC reader.\n");
	}

	return 0;
}

int main() {
	EmulateNTag();
	return 0;
}
