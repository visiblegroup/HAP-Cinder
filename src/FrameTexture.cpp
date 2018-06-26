#include <iostream>
#include "FrameTexture.h"
#include "glad.h"

using namespace glvideo;

FrameTexture::FrameTexture( GLuint pbo, GLsizei imageSize, Format format ) :
        m_target( GL_TEXTURE_2D )
{
    glGenTextures( 1, &m_tex );
    glBindTexture( m_target, m_tex );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER, pbo );
    if ( format.compressed() ) {
        glCompressedTexImage2D( m_target,
                                0,
                                format.internalFormat(),
                                format.width(),
                                format.height(),
                                0,
                                imageSize,
                                NULL );
        glHint( GL_TEXTURE_COMPRESSION_HINT, GL_NICEST );
    } else {
        glTexImage2D( m_target,
                      0,
                      format.internalFormat(),
                      format.width(),
                      format.height(),
                      0,
                      format.format(),
                      GL_UNSIGNED_BYTE,
                      NULL );
    }
    glTexParameteri( m_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( m_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

    glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
    glBindTexture( m_target, 0 );
}

FrameTexture::~FrameTexture()
{
    if ( m_ownsTexture && m_tex ) glDeleteTextures( 1, &m_tex );
}
