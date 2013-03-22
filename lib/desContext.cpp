/*
 * Copyright (c) 2000-2001 Apple Computer, Inc. All Rights Reserved.
 * 
 * The contents of this file constitute Original Code as defined in and are
 * subject to the Apple Public Source License Version 1.2 (the 'License').
 * You may not use this file except in compliance with the License. Please obtain
 * a copy of the License at http://www.apple.com/publicsource and read it before
 * using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS
 * OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES, INCLUDING WITHOUT
 * LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. Please see the License for the
 * specific language governing rights and limitations under the License.
 */


/*
 * desContext.cpp - glue between BlockCrytpor and DES implementation
 * Written by Doug Mitchell 3/28/2001
 */
 
#include "desContext.h"
#include <security_utilities/debugging.h>
#include <security_utilities/globalizer.h>
#include <security_utilities/threading.h>

ModuleNexus<Mutex> desInitMutex;

#define DESDebug(args...)	secdebug("desContext", ## args)

/*
 * DES encrypt/decrypt.
 */
DESContext::~DESContext()
{
	desdone(&DesInst);
	memset(&DesInst, 0, sizeof(struct _desInst));
}
	
/* 
 * Standard CSPContext init, called from CSPFullPluginSession::init().
 * Reusable, e.g., query followed by en/decrypt.
 */
void DESContext::init( 
	const Context &context, 
	bool encrypting)
{
	UInt32 		keyLen;
	UInt8 		*keyData 	= NULL;
	
	/* obtain key from context */
	symmetricKeyBits(context, session(), CSSM_ALGID_DES, 
		encrypting ? CSSM_KEYUSE_ENCRYPT : CSSM_KEYUSE_DECRYPT,	
		keyData, keyLen);
	if(keyLen != (DES_KEY_SIZE_BITS_EXTERNAL / 8)) {
		CssmError::throwMe(CSSMERR_CSP_INVALID_ATTR_KEY);
	}
	
	/* init the low-level state */
	{
		StLock<Mutex> _(desInitMutex());
		if(IFDEBUG(int irtn =) desinit(&DesInst, DES_MODE_STD)) {
			DESDebug("desinit returned %d\n", irtn);
			CssmError::throwMe(CSSMERR_CSP_MEMORY_ERROR);
		}
	}
	dessetkey(&DesInst, (char *)keyData);

	/* Finally, have BlockCryptor do its setup */
	setup(DES_BLOCK_SIZE_BYTES, context);
}	

/*
 * Functions called by BlockCryptor
 * DES does encrypt/decrypt in place
 */
void DESContext::encryptBlock(
	const void		*plainText,			// length implied (one block)
	size_t			plainTextLen,
	void			*cipherText,	
	size_t			&cipherTextLen,		// in/out, throws on overflow
	bool			final)				// ignored
{
	if(plainTextLen != DES_BLOCK_SIZE_BYTES) {
		CssmError::throwMe(CSSMERR_CSP_INPUT_LENGTH_ERROR);
	}
	if(cipherTextLen < DES_BLOCK_SIZE_BYTES) {
		CssmError::throwMe(CSSMERR_CSP_OUTPUT_LENGTH_ERROR);
	}
	if(plainText != cipherText) {
		/* little optimization for callers who want to encrypt in place */
		memmove(cipherText, plainText, DES_BLOCK_SIZE_BYTES);
	}
	endes(&DesInst, (char *)cipherText);
	cipherTextLen = DES_BLOCK_SIZE_BYTES;
}

void DESContext::decryptBlock(
	const void		*cipherText,		// length implied (one block)
	void			*plainText,	
	size_t			&plainTextLen,		// in/out, throws on overflow
	bool			final)				// ignored
{
	if(plainTextLen < DES_BLOCK_SIZE_BYTES) {
		CssmError::throwMe(CSSMERR_CSP_OUTPUT_LENGTH_ERROR);
	}
	if(plainText != cipherText) {
		/* little optimization for callers who want to decrypt in place */
		memmove(plainText, cipherText, DES_BLOCK_SIZE_BYTES);
	}
	dedes(&DesInst, (char *)plainText);
	plainTextLen = DES_BLOCK_SIZE_BYTES;
}

/***
 *** Triple-DES - EDE, 24-bit key only
 ***/
 
DES3Context::~DES3Context()
{
	for(int i =0; i<3; i++) {
		desdone(&DesInst[i]);
		memset(&DesInst[i], 0, sizeof(struct _desInst));
	}
}

/* 
 * Standard CSPContext init, called from CSPFullPluginSession::init().
 * Reusable, e.g., query followed by en/decrypt.
 */
void DES3Context::init( 
	const Context &context, 
	bool encrypting)
{
	UInt32 		keyLen;
	UInt8 		*keyData 	= NULL;
	
	/* obtain key from context */
	symmetricKeyBits(context, session(), CSSM_ALGID_3DES_3KEY_EDE, 
		encrypting ? CSSM_KEYUSE_ENCRYPT : CSSM_KEYUSE_DECRYPT,
		keyData, keyLen);
	if(keyLen != DES3_KEY_SIZE_BYTES) {
		CssmError::throwMe(CSSMERR_CSP_INVALID_ATTR_KEY);
	}
	
	/* init the low-level state */
	int irtn;
	unsigned i;
	{
		StLock<Mutex> _(desInitMutex());
		for(i=0; i<3; i++) {
			if((irtn = desinit(&DesInst[i], DES_MODE_STD))) {
				DESDebug("desinit returned %d\n", irtn);
				CssmError::throwMe(CSSMERR_CSP_MEMORY_ERROR);
			}
			dessetkey(&DesInst[i], (char *)keyData + (8 * i));
		}
	}
	
	/* Finally, have BlockCryptor do its setup */
	setup(DES3_BLOCK_SIZE_BYTES, context);
}	

/*
 * Functions called by BlockCryptor
 * DES does encrypt/decrypt in place
 */
void DES3Context::encryptBlock(
	const void		*plainText,			// length implied (one block)
	size_t			plainTextLen,
	void			*cipherText,	
	size_t			&cipherTextLen,		// in/out, throws on overflow
	bool			final)				// ignored
{
	if(plainTextLen != DES3_BLOCK_SIZE_BYTES) {
		CssmError::throwMe(CSSMERR_CSP_INPUT_LENGTH_ERROR);
	}
	if(cipherTextLen < DES3_BLOCK_SIZE_BYTES) {
		CssmError::throwMe(CSSMERR_CSP_OUTPUT_LENGTH_ERROR);
	}
	if(plainText != cipherText) {
		/* little optimization for callers who want to encrypt in place */
		memmove(cipherText, plainText, DES3_BLOCK_SIZE_BYTES);
	}
	
	/* encrypt --> decrypt --> encrypt */
	endes(&DesInst[0], (char *)cipherText);
	dedes(&DesInst[1], (char *)cipherText);
	endes(&DesInst[2], (char *)cipherText);
	cipherTextLen = DES3_BLOCK_SIZE_BYTES;
}

void DES3Context::decryptBlock(
	const void		*cipherText,		// length implied (one block)
	void			*plainText,	
	size_t			&plainTextLen,		// in/out, throws on overflow
	bool			final)				// ignored
{
	if(plainTextLen < DES3_BLOCK_SIZE_BYTES) {
		CssmError::throwMe(CSSMERR_CSP_OUTPUT_LENGTH_ERROR);
	}
	if(plainText != cipherText) {
		/* little optimization for callers who want to decrypt in place */
		memmove(plainText, cipherText, DES3_BLOCK_SIZE_BYTES);
	}
	
	/* decrypt --> encrypt -->decrypt */
	dedes(&DesInst[2], (char *)plainText);
	endes(&DesInst[1], (char *)plainText);
	dedes(&DesInst[0], (char *)plainText);
	plainTextLen = DES3_BLOCK_SIZE_BYTES;
}
