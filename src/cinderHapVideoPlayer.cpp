
#include "cinderHapVideoPlayer.h"

#include "cinder\gl\gl.h"
#include "cinder\app\App.h"

#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>
#include <stdint.h>
#include <objbase.h>
#include "cinder/Log.h"




#pragma comment(lib,"Strmiids.lib")
#include "DSShared.h"
#include "DSUncompressedSampleGrabber.h"
#include "snappy.h"
#include "snappy-c.h"
#include "snappy-sinksource.h"

using namespace ci;
using namespace ci::app;

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
// DirectShow includes and helper methods 
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------------

#include <dshow.h>
#pragma include_alias( "dxtrans.h", "qedit.h" )
#define __IDxtCompositor_INTERFACE_DEFINED__
#define __IDxtAlphaSetter_INTERFACE_DEFINED__
#define __IDxtJpeg_INTERFACE_DEFINED__
#define __IDxtKey_INTERFACE_DEFINED__




#include <aviriff.h>
#include <windows.h>

//for threading
#include <process.h>

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
// DirectShowVideo - contains a simple directshow video player implementation
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------------

DEFINE_GUID(CLSID_SampleGrabber,
	0xb62f694e, 0x593, 0x4e60, 0xaa, 0x1c, 0x16, 0xaf, 0x64, 0x96, 0xac, 0x39);

DEFINE_GUID(IID_ISampleGrabber,
	0xbe5b5e, 0xcca0, 0x4a9f, 0xb1, 0x98, 0xdf, 0x2f, 0xac, 0xe8, 0x2d, 0x57);

static int comRefCount = 0;

static void retainCom() {
	if (comRefCount == 0) {
		//printf("com is initialized!\n"); 
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	}
	comRefCount++;
}

static void releaseCom() {
	comRefCount--;
	if (comRefCount == 0) {
		//printf("com is uninitialized!\n"); 
		CoUninitialize();
	}
}

class DirectShowHapVideo : public ISampleGrabberCB {

public:

	DirectShowHapVideo() {
		retainCom();
		clearValues();
		InitializeCriticalSection(&critSection);
	}

	~DirectShowHapVideo() {
		stop();
		tearDown();
		releaseCom();
		DeleteCriticalSection(&critSection);
	}

	void tearDown() {

		//printf("tearDown()\n"); 

		if (controlInterface) {
			controlInterface->Stop();
			controlInterface->Release();
		}
		if (eventInterface) {
			eventInterface->Release();
		}
		if (seekInterface) {
			seekInterface->Release();
		}
		if (audioInterface) {
			audioInterface->Release();
		}
		if (positionInterface) {
			positionInterface->Release();
		}
		if (asyncReaderFilterInterface) {
			asyncReaderFilterInterface->Release();
		}
		if (filterGraphManager) {
			this->filterGraphManager->RemoveFilter(this->aviSplitterFilter);
			this->filterGraphManager->RemoveFilter(this->asyncReaderFilter);
			this->filterGraphManager->RemoveFilter(this->rawSampleGrabberFilter);
			this->filterGraphManager->RemoveFilter(this->nullRendererFilter);
			this->filterGraphManager->RemoveFilter(this->audioRendererFilter);
			filterGraphManager->Release();
		}
		if (aviSplitterFilter) {
			aviSplitterFilter->Release();
		}
		if (asyncReaderFilter) {
			asyncReaderFilter->Release();
		}
		if (nullRendererFilter) {
			nullRendererFilter->Release();
		}
		if (audioRendererFilter) {
			audioRendererFilter->Release();
		}
		if (rawSampleGrabberFilter) {
			rawSampleGrabberFilter->SetCallback(NULL, 0);
			rawSampleGrabberFilter->Release();
		}
		if (rawBuffer) {
			delete[] rawBuffer;
		}
		clearValues();
	}

	void clearValues() {

		//cout << "clearValues()\n";

		hr = 0;

		filterGraphManager = NULL;
		controlInterface = NULL;
		eventInterface = NULL;
		seekInterface = NULL;
		audioInterface = NULL;
		m_pGrabber = NULL;
		m_pGrabberF = NULL;
		nullRendererFilter = NULL;
		m_pSourceFile = NULL;
		positionInterface = NULL;
		rawSampleGrabberFilter = NULL;
		asyncReaderFilter = NULL;
		asyncReaderFilterInterface = NULL;
		aviSplitterFilter = NULL;
		audioRendererFilter = NULL;
		rawBuffer = NULL;

		timeFormat = TIME_FORMAT_MEDIA_TIME;
		timeNow = 0;
		lPositionInSecs = 0;
		lDurationInNanoSecs = 0;
		lTotalDuration = 0;
		rtNew = 0;
		lPosition = 0;
		lvolume = -1000;
		evCode = 0;
		width = height = 0;
		videoSize = 0;
		bVideoOpened = false;
		bLoop = true;
		bPaused = false;
		bPlaying = false;
		bEndReached = false;
		bNewPixels = false;
		bFrameNew = false;
		curMovieFrame = -1;
		frameCount = -1;
		lastBufferSize = 0;

		movieRate = 1.0;
		averageTimePerFrame = 1.0 / 30.0;
	}

	//------------------------------------------------
	STDMETHODIMP_(ULONG) AddRef() { return 1; }
	STDMETHODIMP_(ULONG) Release() { return 2; }

	//------------------------------------------------
	STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) {
		*ppvObject = static_cast<ISampleGrabberCB*>(this);
		return S_OK;
	}

	//------------------------------------------------
	STDMETHODIMP SampleCB(long Time, IMediaSample *pSample) {

		BYTE * ptrBuffer = NULL;
		HRESULT hr = pSample->GetPointer(&ptrBuffer);

		if (hr == S_OK) {

			long latestBufferLength = pSample->GetActualDataLength();

			printf("Buffer len %i %i\n", (int)latestBufferLength, (int)videoSize);
			printf("Expected len %i\n", width * height / 2);

			// length hidden in 4-byte header
			int sz = (*(uint8_t *)ptrBuffer) + ((*((uint8_t *)(ptrBuffer)+1)) << 8) + ((*((uint8_t *)(ptrBuffer)+2)) << 16);

			// 1036800

			size_t uncompressedLen = ((width + 3) / 4)*((height + 3) / 4) * 8;

			lastBufferSize = uncompressedLen;
			//lastBufferSize = sz;

			if (rawBuffer != NULL) { // latestBufferLength == videoSize && 

				EnterCriticalSection(&critSection);

				if (sz == uncompressedLen) {

					// copy buffer minus 4 byte yeader
					memcpy(rawBuffer, ptrBuffer + 4, sz);

				}
				else {

					printf("we've got snappy compression!\n");
					printf("Encoded len %i\n", sz);
					printf("Expected len %i\n", ((width + 3) / 4)*((height + 3) / 4) * 8);
					printf("Buffer size %i\n\n", videoSize);

					uncompressedLen = videoSize; //?
					snappy_status SnappyStatus; //M
					SnappyStatus=snappy_uncompress((const char*)ptrBuffer + 4, sz, (char*)rawBuffer, &uncompressedLen); //M
					
					printf("testtest\n");
				}

				bNewPixels = true;

				//this->cond.broadcast();

				//this is just so we know if there is a new frame
				frameCount++;

				LeaveCriticalSection(&critSection);
			}
			else {
				printf("ERROR: SampleCB() - raw buffer is NULL\n");
			}
		}

		return S_OK;
	}

	//This method is meant to have more overhead
	STDMETHODIMP BufferCB(double Time, BYTE *pBuffer, long BufferLen) {
		return E_NOTIMPL;
	}

	bool loadMovieManualGraph(std::string path) {

		//Release all the filters etc.
		tearDown();

		bool success = true;

		this->createFilterGraphManager(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->createAsyncReaderFilter(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->createAviSplitterFilter(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->createRawSampleGrabberFilter(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->createNullRendererFilter(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->querySeekInterface(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->queryPositionInterface(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->queryAudioInterface(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->queryControlInterface(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->queryEventInterface(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->queryAsyncReadFilterInterface(success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->addFilter(this->aviSplitterFilter, L"AVISplitter", success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->addFilter(this->asyncReaderFilter, L"AsyncReader", success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->addFilter(this->rawSampleGrabberFilter, L"RawSampleGrabber", success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->addFilter(this->nullRendererFilter, L"Render", success);

		if (success == false) {
			tearDown();
			return false;
		}

		this->asyncReaderFilterInterfaceLoad(path, success);

		if (success == false) {
			tearDown();
			return false;
		}

		IPin * asyncReaderOutput = this->getOutputPin(asyncReaderFilter, success);
		IPin * aviSplitterInput = this->getInputPin(aviSplitterFilter, success);

		this->connectPins(asyncReaderOutput, aviSplitterInput, success);

		asyncReaderOutput->Release();
		aviSplitterInput->Release();

		IPin * aviSplitterOutput = this->getOutputPin(this->aviSplitterFilter, success);
		IPin * rawSampleGrabberInput = this->getInputPin(this->rawSampleGrabberFilter, success);

		this->connectPins(aviSplitterOutput, rawSampleGrabberInput, success);

		aviSplitterOutput->Release();
		rawSampleGrabberInput->Release();

		// check if file contains audio

		IPin * aviSplitterAudioOutput = NULL;

		if (getContainsAudio(aviSplitterFilter, aviSplitterAudioOutput)) {
			this->createAudioRendererFilter(success);
			this->addFilter(this->audioRendererFilter, L"SoundRenderer", success);

			IPin * audioInputPin = this->getInputPin(audioRendererFilter, success);

			this->connectPins(aviSplitterAudioOutput, audioInputPin, success);

			aviSplitterAudioOutput->Release();
			audioInputPin->Release();
		}

		IPin * rawSampleGrabberOutput = this->getOutputPin(this->rawSampleGrabberFilter, success);

		// No need to connect nullRenderer. AM_RENDEREX_RENDERTOEXISTINGRENDERS requires that the input pin is not connected
		hr = this->filterGraphManager->RenderEx(rawSampleGrabberOutput, AM_RENDEREX_RENDERTOEXISTINGRENDERERS, NULL);

		this->getDimensionsAndFrameInfo(success);

		updatePlayState();

		bVideoOpened = true;

		return true;
	}

	void createFilterGraphManager(bool &success)
	{
		HRESULT hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void **)&filterGraphManager);
		if (FAILED(hr)) {
			success = false;
		}
	}

	void querySeekInterface(bool &success)
	{
		//Allow the ability to go to a specific frame
		HRESULT hr = this->filterGraphManager->QueryInterface(IID_IMediaSeeking, (void**)&this->seekInterface);
		if (FAILED(hr)) {
			success = false;
		}
	}

	void queryPositionInterface(bool &success)
	{
		//Allows the ability to set the rate and query whether forward and backward seeking is possible
		HRESULT hr = this->filterGraphManager->QueryInterface(IID_IMediaPosition, (LPVOID *)&this->positionInterface);
		if (FAILED(hr)) {
			success = false;
		}
	}

	void queryAudioInterface(bool &success)
	{
		//Audio settings interface
		HRESULT hr = this->filterGraphManager->QueryInterface(IID_IBasicAudio, (void**)&this->audioInterface);
		if (FAILED(hr)) {
			success = false;
		}
	}

	void queryControlInterface(bool &success)
	{
		// Control flow of data through the filter graph. I.e. run, pause, stop
		HRESULT hr = this->filterGraphManager->QueryInterface(IID_IMediaControl, (void **)&this->controlInterface);
		if (FAILED(hr)) {
			success = false;
		}
	}

	void queryEventInterface(bool &success)
	{
		// Media events
		HRESULT hr = this->filterGraphManager->QueryInterface(IID_IMediaEvent, (void **)&this->eventInterface);
		if (FAILED(hr)) {
			success = false;
		}
	}

	void queryAsyncReadFilterInterface(bool &success)
	{
		HRESULT hr = asyncReaderFilter->QueryInterface(IID_IFileSourceFilter, (void**)&asyncReaderFilterInterface);
		if (FAILED(hr)) {
			success = false;
		}
	}

	void asyncReaderFilterInterfaceLoad(std::string path, bool &success)
	{
		std::wstring filePathW = std::wstring(path.begin(), path.end());

		HRESULT hr = asyncReaderFilterInterface->Load(filePathW.c_str(), NULL);
		if (FAILED(hr)) {
			// ofLogError("ofxDSHapVideoPlayer") << "Failed to load file " << path;			
			success = false;
		}
	}


	void createAsyncReaderFilter(bool &success) {
		HRESULT hr = CoCreateInstance(CLSID_AsyncReader, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)(&this->asyncReaderFilter));
		if (FAILED(hr)) {
			success = false;
		}
	}

	void createAviSplitterFilter(bool &success) {
		HRESULT hr = CoCreateInstance(CLSID_AviSplitter, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)(&this->aviSplitterFilter));
		if (FAILED(hr)) {
			success = false;
		}
	}

	void createRawSampleGrabberFilter(bool &success) {

		HRESULT hr = 0;
		this->rawSampleGrabberFilter = (DSUncompressedSampleGrabber*)DSUncompressedSampleGrabber::CreateInstance(NULL, &hr);		
		this->rawSampleGrabberFilter->AddRef();
		if (FAILED(hr)) {
			success = false;
		}

		this->rawSampleGrabberFilter->SetCallback(this, 0);
	}

	void createNullRendererFilter(bool &success) {
		HRESULT hr = CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)(&this->nullRendererFilter));
		if (FAILED(hr)) {
			success = false;
		}
	}

	void createAudioRendererFilter(bool &success)
	{
		HRESULT hr = CoCreateInstance(CLSID_DSoundRender, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)(&this->audioRendererFilter));
		if (FAILED(hr)) {
			success = false;
		}
	}

	void addFilter(IBaseFilter * filter, LPCWSTR filterName, bool &success)
	{
		HRESULT hr = this->filterGraphManager->AddFilter(filter, filterName);
		if (FAILED(hr)) {
			success = false;
		}
	}

	bool hasPins(IBaseFilter * filter)
	{
		IEnumPins * enumPins;
		IPin * pin;

		filter->EnumPins(&enumPins);
		enumPins->Reset();
		int numPins = 0;
		while (enumPins->Next(1, &pin, 0) == S_OK) {
			numPins++;
		}

		if (numPins == 0) {
			return false;
		}

		enumPins->Release();
		return true;
	}

	IPin * getOutputPin(IBaseFilter * filter, bool &success)
	{
		IEnumPins * enumPins;
		IPin * pin;
		ULONG fetched;
		PIN_INFO pinfo;

		filter->EnumPins(&enumPins);
		enumPins->Reset();
		enumPins->Next(1, &pin, &fetched);
		if (pin == NULL) {
			success = false;
		}
		else
		{
			pin->QueryPinInfo(&pinfo);
			if (pinfo.dir == PINDIR_INPUT) {
				pin->Release();
				enumPins->Next(1, &pin, &fetched);
			}

			if (pinfo.dir != PINDIR_OUTPUT) {
				success = false;
			}
		}
		enumPins->Release();

		return pin;
	}

	IPin * getInputPin(IBaseFilter * filter, bool &success)
	{
		IEnumPins * enumPins;
		IPin * pin;
		ULONG fetched;
		PIN_INFO pinfo;
		filter->EnumPins(&enumPins);
		enumPins->Reset();
		enumPins->Next(1, &pin, &fetched);
		pin->QueryPinInfo(&pinfo);
		pinfo.pFilter->Release();
		if (pinfo.dir == PINDIR_OUTPUT) {
			pin->Release();
			enumPins->Next(1, &pin, &fetched);
		}
		if (pin == NULL) {
			success = false;
		}
		else
		{
			pin->QueryPinInfo(&pinfo);
			pinfo.pFilter->Release();
			if (pinfo.dir != PINDIR_INPUT)
			{
				success = false;
			}
		}
		enumPins->Release();

		return pin;
	}

	void connectPins(IPin * pinOut, IPin * pinIn, bool &success)
	{
		HRESULT hr = filterGraphManager->ConnectDirect(pinOut, pinIn, NULL);
		if (FAILED(hr)) {
			success = false;
		}
	}

	void getDimensionsAndFrameInfo(bool &success)
	{
		HRESULT hr = this->controlInterface->Run();

		// grab file dimensions and frame info

		AM_MEDIA_TYPE mt;
		ZeroMemory(&mt, sizeof(AM_MEDIA_TYPE));

		hr = rawSampleGrabberFilter->GetMediaType(0, (CMediaType*)&mt);

		if (mt.pbFormat != NULL) {

			VIDEOINFOHEADER * infoheader = (VIDEOINFOHEADER*)mt.pbFormat;
			this->width = infoheader->bmiHeader.biWidth;
			this->height = infoheader->bmiHeader.biHeight;
			this->averageTimePerFrame = infoheader->AvgTimePerFrame / 10000000.0;
			this->videoSize = infoheader->bmiHeader.biSizeImage; // how many pixels to allocate

																 //printf("dims %i %i %i\n", width, height, videoSize);

			BITMAPINFOHEADER * bitmapheader = &((VIDEOINFOHEADER*)mt.pbFormat)->bmiHeader;

			//printf("test %i\n", bitmapheader->biCompression == FOURCC_DXT5);
			//printf("test %i\n", bitmapheader->biCompression == FOURCC_dxt5);
			//printf("hap %i\n", bitmapheader->biCompression == FOURCC_HAP);
			//printf("hapa %i\n", bitmapheader->biCompression == FOURCC_HAPA);
			//printf("hapq %i\n", bitmapheader->biCompression == FOURCC_HAPQ); // this works
			//printf("test %i\n", bitmapheader->biCompression == FOURCC_hapa);

			if (bitmapheader->biCompression == FOURCC_HAP)
			{
				//printf("texture format is HapTextureFormat_RGB_DXT1\n");
				this->textureFormat = HapTextureFormat_RGB_DXT1;
			}
			else if (bitmapheader->biCompression == FOURCC_HAPA)
			{
				//printf("texture format is HapTextureFormat_RGBA_DXT5\n");
				this->textureFormat = HapTextureFormat_RGBA_DXT5;
			}
			else if (bitmapheader->biCompression == FOURCC_HAPQ)
			{
				//printf("texture format is HapTextureFormat_YCoCg_DXT5\n");
				this->textureFormat = HapTextureFormat_YCoCg_DXT5;
			}
			else
			{
				success = false;
			}
		}
		else
		{
			success = false;
		}

		if (width == 0 || height == 0) {
			success = false;
		}
		else {

			if (videoSize == 0) {

				// ofLogWarning("ofxDSHapVideoPlayer") << "Video frame size not encoded in file header";
				videoSize = width * height * (this->textureFormat == HapTextureFormat_RGBA_DXT5 ? 4 : 3);
			}

			this->rawBuffer = new unsigned char[videoSize];
		}

		// Now pause the graph.
		hr = controlInterface->Stop();
	}

	bool getContainsAudio(IBaseFilter * filter, IPin *& audioPin) {

		bool bContainsAudio = false;

		IEnumPins * enumPins;
		IPin * pin;
		ULONG fetched;
		PIN_INFO pinfo;
		HRESULT hr;

		filter->EnumPins(&enumPins);
		enumPins->Reset();

		while (enumPins->Next(1, &pin, &fetched) == S_OK) {

			pin->QueryPinInfo(&pinfo);
			pinfo.pFilter->Release();

			if (pinfo.dir == PINDIR_OUTPUT) {

				//wprintf(L"name %s\n", pinfo.achName);

				// check to see if avi splitter has audio output

				IEnumMediaTypes * pEnum = NULL;
				AM_MEDIA_TYPE * pmt = NULL;
				BOOL bFound = false;

				hr = pin->EnumMediaTypes(&pEnum);

				while (pEnum->Next(1, &pmt, NULL) == S_OK) {

					if (pmt->majortype == MEDIATYPE_Audio) {

						audioPin = pin;

						bContainsAudio = true;
					}

					//printf("major %s, subtype %s\n", pmt->majortype, pmt->subtype);
				}
			}
		}

		enumPins->Release();

		return bContainsAudio;
	}

	void update() {

		if (bVideoOpened) {

			long eventCode = 0;
			LONG_PTR ptrParam1 = 0;
			LONG_PTR ptrParam2 = 0;
			long timeoutMs = 2000;

			if (curMovieFrame != frameCount) {
				bFrameNew = true;
			}
			else {
				bFrameNew = false;
			}
			curMovieFrame = frameCount;

			while (S_OK == eventInterface->GetEvent(&eventCode, (LONG_PTR*)&ptrParam1, (LONG_PTR*)&ptrParam2, 0)) {
				if (eventCode == EC_COMPLETE) {
					if (bLoop) {
						//printf("Restarting!\n");
						setPosition(0.0);
						frameCount = 0;
					}
					else {
						bEndReached = true;
						//printf("movie end reached!\n");
						stop();
						updatePlayState();
					}
				}
				//printf("Event code: %#04x\n Params: %d, %d\n", eventCode, ptrParam1, ptrParam2);
				eventInterface->FreeEventParams(eventCode, ptrParam1, ptrParam2);

			}
		}
	}

	bool isLoaded() {
		return bVideoOpened;
	}

	//volume has to be log corrected/converted
	void setVolume(float volPct) {
		if (isLoaded()) {
			if (volPct < 0) volPct = 0.0;
			if (volPct > 1) volPct = 1.0;

			long vol = log10(volPct) * 4000.0;
			audioInterface->put_Volume(vol);
		}
	}

	float getVolume() {
		float volPct = 0.0;
		if (isLoaded()) {
			long vol = 0;
			audioInterface->get_Volume(&vol);
			volPct = powf(10, (float)vol / 4000.0);
		}
		return volPct;
	}

	double getDurationInSeconds() {
		if (this->timeFormat != TIME_FORMAT_MEDIA_TIME)
		{
			seekInterface->SetTimeFormat(&TIME_FORMAT_MEDIA_TIME);
			this->timeFormat = TIME_FORMAT_MEDIA_TIME;
		}
		if (isLoaded()) {
			long long lDurationInNanoSecs = 0;
			seekInterface->GetDuration(&lDurationInNanoSecs);
			double timeInSeconds = (double)lDurationInNanoSecs / 10000000.0;

			return timeInSeconds;
		}
		return 0.0;
	}

	double getCurrentTimeInSeconds() {

		if (this->timeFormat != TIME_FORMAT_MEDIA_TIME) {
			seekInterface->SetTimeFormat(&TIME_FORMAT_MEDIA_TIME);
			this->timeFormat = TIME_FORMAT_MEDIA_TIME;
		}
		if (isLoaded()) {
			long long lCurrentTimeInNanoSecs = 0;
			seekInterface->GetCurrentPosition(&lCurrentTimeInNanoSecs);
			double timeInSeconds = (double)lCurrentTimeInNanoSecs / 10000000.0;
			return timeInSeconds;
		}
		return 0.0;

	}

	void setPosition(float pct) {

		if (this->timeFormat != TIME_FORMAT_MEDIA_TIME)	{
			seekInterface->SetTimeFormat(&TIME_FORMAT_MEDIA_TIME);
			this->timeFormat = TIME_FORMAT_MEDIA_TIME;
		}

		if (bVideoOpened) {
			if (pct < 0.0) pct = 0.0;
			if (pct > 1.0) pct = 1.0;

			long long lDurationInNanoSecs = 0;
			seekInterface->GetDuration(&lDurationInNanoSecs);

			rtNew = ((float)lDurationInNanoSecs * pct);
			hr = seekInterface->SetPositions(&rtNew, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
		}
	}

	float getPosition() {
		if (this->timeFormat != TIME_FORMAT_MEDIA_TIME)
		{
			seekInterface->SetTimeFormat(&TIME_FORMAT_MEDIA_TIME);
			this->timeFormat = TIME_FORMAT_MEDIA_TIME;
		}
		if (bVideoOpened) {
			float timeDur = getDurationInSeconds();
			if (timeDur > 0.0) {
				return getCurrentTimeInSeconds() / timeDur;
			}
		}
		return 0.0;
	}

	void setSpeed(float speed) {
		if (bVideoOpened) {
			positionInterface->put_Rate(speed);
			positionInterface->get_Rate(&movieRate);
		}
	}

	double getSpeed() {
		return movieRate;
	}

	HapTextureFormat getHapTextureFormat() {
		return textureFormat;
	}

	void play() {
		if (bVideoOpened) {
			controlInterface->Run();
			bEndReached = false;
			updatePlayState();
		}
	}

	void stop() {
		if (bVideoOpened) {
			if (isPlaying()) {
				setPosition(0.0);
			}
			controlInterface->Stop();
			updatePlayState();
		}
	}

	void setPaused(bool bPaused) {
		if (bVideoOpened) {
			if (bPaused) {
				controlInterface->Pause();
			}
			else {
				controlInterface->Run();
			}
			updatePlayState();
		}
	}

	void updatePlayState() {
		if (bVideoOpened) {
			FILTER_STATE fs;
			hr = controlInterface->GetState(4000, (OAFilterState*)&fs);
			if (hr == S_OK) {
				if (fs == State_Running) {
					bPlaying = true;
					bPaused = false;
				}
				else if (fs == State_Paused) {
					bPlaying = false;
					bPaused = true;
				}
				else if (fs == State_Stopped) {
					bPlaying = false;
					bPaused = false;
				}
			}
		}
	}

	bool isPlaying() {
		updatePlayState();
		return bPlaying;
	}

	bool isPaused() {
		updatePlayState();
		return bPaused;
	}

	bool isLooping() {
		return bLoop;
	}

	void setLoop(bool loop) {
		bLoop = loop;
	}

	bool isMovieDone() {
		return bEndReached;
	}

	float getWidth() {
		return width;
	}

	float getHeight() {
		return height;
	}

	bool isFrameNew() {
		return bFrameNew;
	}

	void nextFrame() {
		//we have to do it like this as the frame based approach is not very accurate
		if (bVideoOpened && (isPlaying() || isPaused())) {
			int curFrame = getCurrentFrame();
			int totalFrames = getTotalFrames();
			// setFrame(min(curFrame + 1, totalFrames - 1));
			setFrame((curFrame + 1) < (totalFrames - 1) ? (curFrame + 1) : (totalFrames - 1));
		}
	}

	void previousFrame() {
		//we have to do it like this as the frame based approach is not very accurate
		if (bVideoOpened && (isPlaying() || isPaused())) {
			int curFrame = getCurrentFrame();
			setFrame(0 > (curFrame - 1) ? 0 : (curFrame - 1));
			// setFrame(max(0, curFrame - 1));
		}
	}

	void setFrame(int frame) {
		if (this->timeFormat != TIME_FORMAT_FRAME)
		{
			seekInterface->SetTimeFormat(&TIME_FORMAT_FRAME);
			this->timeFormat = TIME_FORMAT_FRAME;
		}
		if (bVideoOpened) {
			LONGLONG frameNumber = frame;
			hr = seekInterface->SetPositions(&frameNumber, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
		}
	}

	int getCurrentFrame() {
		if (this->timeFormat != TIME_FORMAT_FRAME)
		{
			seekInterface->SetTimeFormat(&TIME_FORMAT_FRAME);
			this->timeFormat = TIME_FORMAT_FRAME;
		}
		LONGLONG currentFrame = 0;
		if (bVideoOpened) {
			seekInterface->GetCurrentPosition(&currentFrame);
		}
		return currentFrame;
	}

	int getTotalFrames() {
		if (this->timeFormat != TIME_FORMAT_FRAME)
		{
			seekInterface->SetTimeFormat(&TIME_FORMAT_FRAME);
			this->timeFormat = TIME_FORMAT_FRAME;
		}
		LONGLONG frames = 0;
		if (isLoaded()) {
			seekInterface->GetDuration(&frames);
		}
		return frames;
	}

	int getBufferSize() {
		return lastBufferSize;
	}

	void getPixels(unsigned char * dstBuffer) {

		if (bVideoOpened && bNewPixels) {

			EnterCriticalSection(&critSection);

			// this isn't uncompressed video, no need to process pixels
			//processPixels(rawBuffer, dstBuffer, width, height, false, false);

			// just copy it over ..
			memcpy(dstBuffer, rawBuffer, videoSize);

			bNewPixels = false;
			LeaveCriticalSection(&critSection);
		}
	}

	HRESULT SaveGraphFile(IGraphBuilder *pGraph, WCHAR *wszPath) {

		const WCHAR wszStreamName[] = L"DSMovieGraph";
		HRESULT hr;

		IStorage * pStorage = NULL;
		hr = StgCreateDocfile(
			wszPath,
			STGM_CREATE | STGM_TRANSACTED | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
			0, &pStorage);

		if (FAILED(hr)) {

			printf("Error: failed to create IStorage\n");
			return hr;
		}

		IStream * pStream;
		hr = pStorage->CreateStream(
			wszStreamName,
			STGM_WRITE | STGM_CREATE | STGM_SHARE_EXCLUSIVE,
			0, 0, &pStream);

		if (FAILED(hr)) {

			printf("Error: failed to create IStream\n");
			pStorage->Release();
			return hr;
		}

		IPersistStream * pPersist = NULL;
		hr = pGraph->QueryInterface(IID_IPersistStream, (void**)&pPersist);

		if (FAILED(hr)) {

			printf("Error: failed to create graph\n");
			return hr;
		}

		hr = pPersist->Save(pStream, TRUE);
		pStream->Release();
		pPersist->Release();
		if (FAILED(hr)) {

			printf("Error: failed to save file\n");
			return hr;
		}


		hr = pStorage->Commit(STGC_DEFAULT);

		if (FAILED(hr)) {

			printf("Error: failed to commit file\n");
		}
		else {

			printf("Created log file\n");
		}

		pStorage->Release();

		return hr;
	}

protected:

	HRESULT hr;							// COM return value
	IFilterGraph2 * filterGraphManager;	// Filter Graph
	IMediaControl *controlInterface;	// Media Control interface
	IMediaEvent   *eventInterface;		// Media Event interface
	IMediaSeeking *seekInterface;		// Media Seeking interface
	IMediaPosition * positionInterface;
	IBasicAudio   *audioInterface;		// Audio Settings interface 
	DSUncompressedSampleGrabber * rawSampleGrabberFilter;
	ISampleGrabber * m_pGrabber;
	IBaseFilter * m_pSourceFile;
	IBaseFilter * m_pGrabberF; // interface?
	IBaseFilter * asyncReaderFilter;
	IFileSourceFilter * asyncReaderFilterInterface;
	IBaseFilter * aviSplitterFilter;
	IBaseFilter * nullRendererFilter;
	IBaseFilter * audioRendererFilter;

	GUID timeFormat;
	REFERENCE_TIME timeNow;			// Used for FF & REW of movie, current time
	LONGLONG lPositionInSecs;		// Time in  seconds
	LONGLONG lDurationInNanoSecs;	// Duration in nanoseconds
	LONGLONG lTotalDuration;		// Total duration
	REFERENCE_TIME rtNew;			// Reference time of movie 
	long lPosition;					// Desired position of movie used in FF & REW
	long lvolume;					// The volume level in 1/100ths dB Valid values range from -10,000 (silence) to 0 (full volume), 0 = 0 dB -10000 = -100 dB 
	long evCode;					// event variable, used to in file to complete wait.

	long width, height;
	long videoSize;

	double averageTimePerFrame;

	bool bFrameNew;
	bool bNewPixels;
	bool bVideoOpened;
	bool bPlaying;
	bool bPaused;
	bool bLoop;
	bool bEndReached;
	double movieRate;
	int curMovieFrame;
	int frameCount;
	int lastBufferSize;

	CRITICAL_SECTION critSection;
	unsigned char * rawBuffer;
	HapTextureFormat textureFormat;
};

#define STRINGIFY(x) #x

/* END OF DIRECT SHOW VIDEO PLAYER CLASS */

cinderHapVideoPlayer::cinderHapVideoPlayer() {
	player = nullptr;
	bShaderInitialized = false;
	width = height = 0;
}

cinderHapVideoPlayer::~cinderHapVideoPlayer() {
	close();
}

bool cinderHapVideoPlayer::open(std::string path) {
	close();
	player = new DirectShowHapVideo();

	bool bOK = player->loadMovieManualGraph(path); // manual graph

	if (bOK)
	{

		width = player->getWidth();
		height = player->getHeight();

		if (width != 0 && height != 0) {
			textureFormat = player->getHapTextureFormat();
			fmt.setTarget(GL_TEXTURE_2D);
			if (textureFormat == HapTextureFormat_RGB_DXT1) {
				fmt.setInternalFormat(GL_COMPRESSED_RGB_S3TC_DXT1_EXT);
				mSurface = ci::Surface8u::create(width, height, false, ci::SurfaceChannelOrder::RGB);
			}
			
			else if (textureFormat == HapTextureFormat_RGBA_DXT5) {
				fmt.setInternalFormat(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);
				mSurface = ci::Surface::create(width, height, true);
			}
			else {
				cout << "HAPQ";
				fmt.setInternalFormat(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);
				mSurface = ci::Surface::create(width, height, false);
			}
			
			mTexture = gl::Texture2d::create(width, height, fmt);
			gl::ScopedTextureBind stb(mTexture->getTarget(), mTexture->getId());
		
		}
	} else {
		cout << "Failed to create OpenGL texture: width and/or height are zero";
		return false;
	}


	if (!bShaderInitialized) {

		std::string ofxHapPlayerVertexShader;
		if (ofIsGLProgrammableRenderer())
		{

			ofxHapPlayerVertexShader = "#version 400\n" STRINGIFY(
				uniform mat4 modelViewMatrix;
				uniform mat4 modelViewProjectionMatrix;
				layout(location = 0) in vec4 position;
				layout(location = 3) in vec2 texcoord;
				out vec2 v_texcoord;

				void main() {
					v_texcoord = texcoord;
					gl_Position = modelViewProjectionMatrix * position;
				}
			);
		} else {
			ofxHapPlayerVertexShader = STRINGIFY(

				in vec4 ciPosition;
				in vec2 ciTexCoord0;
				uniform mat4 ciModelViewProjection;
				out vec2	vTexCoord0;
				void main(void)
				{
					gl_Position = ciModelViewProjection * ciPosition;
					vTexCoord0 = ciTexCoord0;
				});

				/*
				void main(void)
				{
					gl_Position = ftransform();
					gl_TexCoord[0] = gl_MultiTexCoord0;
				});
				*/
		}

		std::string ofxHapPlayerFragmentShader;
		if (ofIsGLProgrammableRenderer())
		{
			ofxHapPlayerFragmentShader = "#version 400\n" STRINGIFY(
				uniform sampler2D src_tex0;
				in vec2 v_texcoord;
				out vec4 v_fragColor;
				uniform float width;
				uniform float height;
				const vec4 offsets = vec4(-0.50196078431373, -0.50196078431373, 0.0, 0.0);
				void main()
				{
					vec4 CoCgSY = texture2D(src_tex0, v_texcoord);
					CoCgSY += offsets;
					float scale = (CoCgSY.z * (255.0 / 8.0)) + 1.0;
					float Co = CoCgSY.x / scale;
					float Cg = CoCgSY.y / scale;
					float Y = CoCgSY.w;
					vec4 rgba = vec4(Y + Co - Cg, Y + Cg, Y - Co - Cg, 1.0);
					v_fragColor = rgba;
				}
			);

		} else {

			ofxHapPlayerFragmentShader = STRINGIFY(

				uniform sampler2D cocgsy_src;
				in vec2	vTexCoord0;
				out vec4 fragColor;
				const vec4 offsets = vec4(-0.50196078431373, -0.50196078431373, 0.0, 0.0);
				void main()
				{
					vec4 CoCgSY = texture(cocgsy_src, vTexCoord0);
					CoCgSY += offsets;
					float scale = (CoCgSY.z * (255.0 / 8.0)) + 1.0;
					float Co = CoCgSY.x / scale;
					float Cg = CoCgSY.y / scale;
					float Y = CoCgSY.w;
					vec4 rgba = vec4(Y + Co - Cg, Y + Cg, Y - Co - Cg, 1.0);
					fragColor = rgba;
				});

				/*
				uniform sampler2D cocgsy_src;
				const vec4 offsets = vec4(-0.50196078431373, -0.50196078431373, 0.0, 0.0);
				void main()
				{
					vec4 CoCgSY = texture2D(cocgsy_src, gl_TexCoord[0].xy);
					CoCgSY += offsets;
					float scale = (CoCgSY.z * (255.0 / 8.0)) + 1.0;
					float Co = CoCgSY.x / scale;
					float Cg = CoCgSY.y / scale;
					float Y = CoCgSY.w;
					vec4 rgba = vec4(Y + Co - Cg, Y + Cg, Y - Co - Cg, 1.0);
					gl_FragColor = rgba;
				});
				*/
		}

		try {
			shader = gl::GlslProg::create(ofxHapPlayerVertexShader, ofxHapPlayerFragmentShader);
		} catch (gl::GlslProgExc &e) {
			std::string errorMsg = e.what();
			return false;
		}

		if (ofIsGLProgrammableRenderer()) {
			shader->uniform("width", (float)width);
			shader->uniform("height", (float)height);
		}
	}

	return bOK;

}

void cinderHapVideoPlayer::close() {
	stop();
	if (player) {
		delete player;
		player = NULL;
	}
}

void cinderHapVideoPlayer::play() { 
	if (player && player->isLoaded()) {
		if (player->isPaused()) {
			player->setPaused(false);
		} else {
			player->play();
		}
	}
}

void cinderHapVideoPlayer::pause() { 
	if (player && player->isLoaded() && player->isPlaying()) {
		player->setPaused(true);
	}
}

void cinderHapVideoPlayer::stop() {
	if (player && player->isLoaded()) {
		player->stop();
	}
}

void cinderHapVideoPlayer::seekToSecond(float second) { 
	
}

void cinderHapVideoPlayer::setLoop(bool loop) { 

}

cinderHapVideoPlayer::HapType cinderHapVideoPlayer::getHapType() {
	return HapType::HAP;
}

void cinderHapVideoPlayer::update() {
	if (player && player->isLoaded()) {
		writeToTexture();
		player->update();
	}
}

void cinderHapVideoPlayer::waitUpdate(long milliseconds) {
	// void
}

string cinderHapVideoPlayer::getGLError() {
	switch (glGetError())
	{
	case GL_NO_ERROR:
		cout << "OK" << endl;
		return "OK";
		break;
	case GL_INVALID_OPERATION:
		cout << "invalid operation" << endl;
		return "invalid operation";
		break;
	case GL_INVALID_ENUM:
		cout << "invalid enum" << endl;
		return "invalid enum";
		break;
	case GL_INVALID_VALUE:
		cout << "invalid value" << endl;
		return "invalid value";
		break;
	case GL_STACK_OVERFLOW:
		cout << "stack overflow" << endl;
		return "stack overflow";
		break;
	case GL_STACK_UNDERFLOW:
		cout << "stack underflow" << endl;
		return "stack underflow";
		break;
	case GL_OUT_OF_MEMORY:
		cout << "out of memory" << endl;
		return "out of memory";
		break;
	default:
		cout << "Unknown openGL error" << endl;
		return "Unknown openGL error";
		break;
	}
}

void cinderHapVideoPlayer::writeToTexture() {
	player->getPixels(mSurface->getData());
	gl::ScopedTextureBind stb(mTexture->getTarget(), mTexture->getId());
	
	GLint imageSize = 0;
	if (textureFormat == HapTextureFormat_RGB_DXT1) {
		imageSize = width * height / 2;
	}
	else {
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &imageSize);
		openGLError = getGLError();
	}
	// Upload compressed data.
	glCompressedTexImage2D(mTexture->getTarget(), 0, fmt.getInternalFormat(), width, height, 0, imageSize, mSurface->getData());
	openGLError = getGLError();
}

void cinderHapVideoPlayer::draw() {

	Rectf centeredRect = Rectf(mTexture->getBounds());
	gl::color(Color::white());

	auto drawRect = [&]() {
		gl::ScopedTextureBind cocgsy_src(mTexture);
		float cw = mTexture->getWidth();
		float ch = mTexture->getHeight();
		float w = mTexture->getWidth();
		float h = mTexture->getHeight();
		//gl::drawSolidRect(centeredRect);
		gl::drawSolidRect(centeredRect, vec2(0, 0), vec2(1, 1)); //This takes care of the vertical flip. Probably there is a better way by using fmt.loadTopDown(true), but I just can't make it work.
	};

	if (textureFormat == HapTextureFormat_YCoCg_DXT5) {
		gl::ScopedGlslProg bind(shader);
		drawRect();
	} else {
		gl::pushMatrices(); //This takes care of the vertical flip. Probably there is a better way by using fmt.loadTopDown(true), but I just can't make it work.
		gl::translate(0, height,0);
		gl::scale(1., -1., 1.);
		gl::draw(mTexture);	
		gl::popMatrices();
	}
}