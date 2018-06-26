#pragma once

// #include "cinder\app\App.h"
#include "cinder\gl\gl.h"
#include "HapShared.h"


using namespace ci;
using namespace std;

class DirectShowHapVideo;

class cinderHapVideoPlayer {

public:

	cinderHapVideoPlayer();
	~cinderHapVideoPlayer();

	void update();
	void waitUpdate(long milliseconds);
	void writeToTexture();
	string getGLError();
	void draw();

	bool open(std::string path);
	void close();

	void play();
	void pause();
	void stop();	
	void seekToSecond(float second);

	void setLoop(bool loop);

	enum HapType { HAP, HAPALPHA, HAPQ };
	HapType	getHapType();

	gl::GlslProgRef shader;
	
	ci::Surface8uRef mSurface;
	ci::Surface8uRef mSurfaceRed;
	unsigned char* rawBufferDevice;

	ci::Surface8uRef mTempSurface;
	ci::Surface8uRef mTempLargeSurface;
	gl::Texture2dRef mTexture;

private:

	bool ofIsGLProgrammableRenderer() { return false; }
	//gl::TextureData texData;
	gl::Texture2d::Format fmt;
	GLint dataLength = 0;
	GLint intFormat;

protected:

	DirectShowHapVideo* player;
	bool bShaderInitialized;

	int width, height;

	string openGLError;	
	//snappy_status SnappyStatus; //M
	
	HapTextureFormat textureFormat;

};
