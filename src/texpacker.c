#include "texpacker/texpacker.h"

#include "jansson.h"

#define STB_IMAGE_IMPLEMENTATION 
#define STBI_NO_STDIO
#define STBI_NO_BMP
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_NO_TGA
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <malloc.h>
#include <string.h>
#include <wchar.h>

//////////////////////////////////////////////////////////////////////////
#define TEXPACKER_NEW(T) (T *)malloc(sizeof(T))
#define TEXPACKER_NEWN(T, N) (T *)malloc(N * sizeof(T))
//////////////////////////////////////////////////////////////////////////
static int texpacker_load_data_buffer( const wchar_t * _path, void ** _buffer, size_t * const _len )
{
    FILE * f = _wfopen( _path, L"rb" );

    if( f == NULL )
    {
        return 1;
    }

    fseek( f, 0L, SEEK_END );
    long sz = ftell( f );
    rewind( f );

    void * data_buffer = malloc( sz );
    fread( data_buffer, 1, sz, f );
    fclose( f );

    *_buffer = data_buffer;
    *_len = (size_t)sz;

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int texpacker_copy_utf8_to_wchar( const char * _utf8, size_t _size, const wchar_t ** const _unicode )
{
    wchar_t * unicode = (wchar_t *)malloc( (_size + 1) * sizeof( wchar_t ) );

    wchar_t * unicode_iter = unicode;
    size_t max_mbs = _size;
    wchar_t dest;
    mbstate_t mbs;
    mbrlen( NULL, 0, &mbs );

    while( max_mbs > 0 )
    {
        size_t s = mbrtowc( &dest, _utf8 + _size - max_mbs, max_mbs, &mbs );

        if( s == 0 || s > max_mbs )
        {
            break;
        }

        *unicode_iter++ = dest;

        max_mbs -= s;
    }

    *unicode_iter = L'\0';

    *_unicode = unicode;

    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct texpacker_atlas_t
{
    uint32_t index;

    uint32_t width;
    uint32_t height;
    uint32_t channel;

    void * pixels;

    wchar_t path[FILENAME_MAX];
} texpacker_atlas_t;
//////////////////////////////////////////////////////////////////////////
typedef struct texpacker_atlas_rect_t
{
    uint32_t x;
    uint32_t y;
    uint32_t u;
    uint32_t v;
    uint32_t w;
    uint32_t h;

    uint32_t state;
    uint8_t rotate;

    struct texpacker_atlas_rect_t * l[4];
} texpacker_atlas_rect_t;
//////////////////////////////////////////////////////////////////////////
typedef struct texpacker_texture_t
{
    const wchar_t * path;

    void * pixels;
    uint32_t width;
    uint32_t height;
    uint32_t channel;

    texpacker_atlas_rect_t * atlas_rect;
    texpacker_atlas_t * atlas;
} texpacker_texture_t;
//////////////////////////////////////////////////////////////////////////
typedef struct texpacker_in_data_t
{
    uint32_t textures_count;
    texpacker_texture_t * textures;

    uint32_t atlas_max_width;
    uint32_t atlas_max_height;
    uint32_t atlas_channels;

    const wchar_t * output_atlas_path;
    const wchar_t * output_atlas_path_ext;
    const wchar_t * output_atlas_path_format;

    const wchar_t * output_atlas_info;
} texpacker_in_data_t;
//////////////////////////////////////////////////////////////////////////
static int texpacker_load_in_data( const void * _buffer, size_t _len, texpacker_in_data_t * const _data )
{
    json_error_t j_error;
    json_t * j = json_loadb( _buffer, _len, 0, &j_error );

    if( j == NULL )
    {
        return 1;
    }

    json_t * j_textures = json_object_get( j, "textures" );

    if( j_textures == NULL )
    {
        return 1;
    }

    uint32_t textures_count = (uint32_t)json_array_size( j_textures );

    texpacker_texture_t * textures = TEXPACKER_NEWN( texpacker_texture_t, textures_count );

    for( uint32_t index = 0; index != textures_count; ++index )
    {
        json_t * j_texture_path = json_array_get( j_textures, index );

        const char * texture_path = json_string_value( j_texture_path );
        size_t texture_path_len = json_string_length( j_texture_path );

        wchar_t * unicode_texture_path;
        if( texpacker_copy_utf8_to_wchar( texture_path, texture_path_len, &unicode_texture_path ) != 0 )
        {
            return 1;
        }

        textures[index].path = unicode_texture_path;
    }

    _data->textures_count = textures_count;
    _data->textures = textures;

    json_t * j_atlas = json_object_get( j, "atlas" );

    if( j_atlas == NULL )
    {
        return 1;
    }

    json_t * j_atlas_max_width = json_object_get( j_atlas, "max_width" );

    if( j_atlas_max_width == NULL )
    {
        return 1;
    }

    json_t * j_atlas_max_height = json_object_get( j_atlas, "max_height" );

    if( j_atlas_max_height == NULL )
    {
        return 1;
    }

    json_t * j_atlas_channels = json_object_get( j_atlas, "channels" );

    if( j_atlas_channels == NULL )
    {
        return 1;
    }

    _data->atlas_max_width = (uint32_t)json_integer_value( j_atlas_max_width );
    _data->atlas_max_height = (uint32_t)json_integer_value( j_atlas_max_height );
    _data->atlas_channels = (uint32_t)json_integer_value( j_atlas_channels );

    json_t * j_output = json_object_get( j, "output" );

    if( j_output == NULL )
    {
        return 1;
    }

    json_t * j_output_atlas_path = json_object_get( j_output, "atlas_path" );

    if( j_output_atlas_path == NULL )
    {
        return 1;
    }

    const char * output_atlas_path = json_string_value( j_output_atlas_path );
    size_t output_atlas_path_len = json_string_length( j_output_atlas_path );

    wchar_t * unicode_output_atlas_path;
    if( texpacker_copy_utf8_to_wchar( output_atlas_path, output_atlas_path_len, &unicode_output_atlas_path ) != 0 )
    {
        return 1;
    }

    _data->output_atlas_path = unicode_output_atlas_path;

    wchar_t * output_atlas_path_ext = wcsrchr( _data->output_atlas_path, '.' );

    if( output_atlas_path_ext == NULL )
    {
        return 1;
    }

    _data->output_atlas_path_ext = output_atlas_path_ext;

    json_t * j_output_atlas_path_format = json_object_get( j_output, "atlas_path_format" );

    if( j_output_atlas_path_format != NULL )
    {
        const char * output_atlas_path_format = json_string_value( j_output_atlas_path_format );
        size_t output_atlas_path_format_len = json_string_length( j_output_atlas_path_format );

        wchar_t * unicode_output_atlas_path_format;
        if( texpacker_copy_utf8_to_wchar( output_atlas_path_format, output_atlas_path_format_len, &unicode_output_atlas_path_format ) != 0 )
        {
            return 1;
        }

        _data->output_atlas_path_format = unicode_output_atlas_path_format;
    }
    else
    {
        _data->output_atlas_path_format = NULL;
    }

    json_t * j_output_atlas_info = json_object_get( j_output, "atlas_info" );

    if( j_output_atlas_info == NULL )
    {
        return 1;
    }

    const char * output_atlas_info = json_string_value( j_output_atlas_info );
    size_t output_atlas_info_len = json_string_length( j_output_atlas_info );

    wchar_t * unicode_output_atlas_info;
    if( texpacker_copy_utf8_to_wchar( output_atlas_info, output_atlas_info_len, &unicode_output_atlas_info ) != 0 )
    {
        return 1;
    }

    _data->output_atlas_info = unicode_output_atlas_info;

    json_decref( j );

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int texpacker_load_texures_pixels( texpacker_in_data_t * const _data )
{
    for( uint32_t index = 0; index != _data->textures_count; ++index )
    {
        texpacker_texture_t * texture = _data->textures + index;

        const wchar_t * texture_path = texture->path;

        void * texure_buffer;
        size_t texure_len;
        if( texpacker_load_data_buffer( texture_path, &texure_buffer, &texure_len ) != 0 )
        {
            return 1;
        }

        int width;
        int height;
        int channel;
        stbi_uc * texture_pixels = stbi_load_from_memory( (stbi_uc *)texure_buffer, texure_len, &width, &height, &channel, 0 );

        texture->pixels = (void *)texture_pixels;
        texture->width = (uint32_t)width;
        texture->height = (uint32_t)height;
        texture->channel = (uint32_t)channel;
        texture->atlas_rect = NULL;
        texture->atlas = NULL;

        printf( "%ls w %dx%d [%d]\n", texture_path, width, height, channel );
    }

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __max_uint32_t( uint32_t _a, uint32_t _b )
{
    return _a > _b ? _a : _b;
}
//////////////////////////////////////////////////////////////////////////
static int __textures_compare_reverse( void const * _el1, void const * _el2 )
{
    texpacker_texture_t * tex1 = (texpacker_texture_t *)_el1;
    texpacker_texture_t * tex2 = (texpacker_texture_t *)_el2;

    uint32_t a1 = tex1->width;
    uint32_t a2 = tex2->width;

    if( a1 < a2 )
    {
        return 1;
    }
    else if( a1 > a2 )
    {
        return -1;
    }
    else
    {
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static int texpacker_load_texures_sort( texpacker_in_data_t * const _data )
{
    qsort( _data->textures, _data->textures_count, sizeof( texpacker_texture_t ), &__textures_compare_reverse );

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static texpacker_atlas_rect_t * texpacker_make_atlas_rect( uint32_t _x, uint32_t _y, uint32_t _width, uint32_t _height )
{
    texpacker_atlas_rect_t * r = TEXPACKER_NEW( texpacker_atlas_rect_t );

    r->x = _x;
    r->y = _y;
    r->u = ~0U;
    r->v = ~0U;
    r->w = _width;
    r->h = _height;

    r->state = 0x00000000;
    r->rotate = 0;

    return r;
}
//////////////////////////////////////////////////////////////////////////
static int texpacker_fill_atlas_rect( texpacker_atlas_rect_t * _r, int8_t _rotate, const texpacker_texture_t * _t )
{
    if( _r->state != 0x00000000 )
    {
        return 1;
    }

    if( _rotate == 0 )
    {
        _r->u = _t->width;
        _r->v = _t->height;
    }
    else
    {
        _r->u = _t->height;
        _r->v = _t->width;
    }

    _r->state |= 0x00000001;
    _r->rotate = _rotate;

    uint32_t w = _r->w;
    uint32_t h = _r->h;

    _r->l[0] = texpacker_make_atlas_rect( _r->x, _r->y + _r->v, w, h - _r->v );
    _r->l[1] = texpacker_make_atlas_rect( _r->x + _r->u, _r->y, w - _r->u, _r->v );
    _r->l[2] = texpacker_make_atlas_rect( _r->x, _r->y + _r->v, _r->u, h - _r->v );
    _r->l[3] = texpacker_make_atlas_rect( _r->x + _r->u, _r->y, w - _r->u, h );

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int texpacker_mark_atlas_rect( texpacker_atlas_rect_t * _r, texpacker_atlas_rect_t * _mr )
{
    if( _r == _mr )
    {
        return 0;
    }

    if( _r->state == 0 )
    {
        return -1;
    }

    const uint32_t masks[4] = {
        (0x00000010 | 0x00000100),
        (0x00000020 | 0x00000100),
        (0x00000040 | 0x00000200),
        (0x00000080 | 0x00000200)
    };

    for( uint32_t index = 0; index != 4; ++index )
    {
        texpacker_atlas_rect_t * rp = _r->l[index];

        if( rp == _mr )
        {
            _r->state |= masks[index];

            return 0;
        }
        else
        {
            if( texpacker_mark_atlas_rect( rp, _mr ) == 0 )
            {
                return 0;
            }
        }
    }

    return -1;
}
//////////////////////////////////////////////////////////////////////////
typedef struct texpacker_atlas_rect_desc_t
{
    texpacker_atlas_rect_t * r;
    int8_t rotate;
} texpacker_atlas_rect_desc_t;
//////////////////////////////////////////////////////////////////////////
static int texpacker_find_atlas_rects( texpacker_atlas_rect_t * _r, texpacker_texture_t * _t, texpacker_atlas_rect_desc_t * _nr, uint32_t * const _count )
{
    uint32_t w = _t->width;
    uint32_t h = _t->height;

    switch( _r->state & 0x0000000F )
    {
    case 0:
        {
            if( (_r->w >= w && _r->h >= h) )
            {
                texpacker_atlas_rect_desc_t desc;
                desc.r = _r;
                desc.rotate = 0;

                _nr[(*_count)++] = desc;
            }
            else if( (_r->w >= h && _r->h >= w) )
            {
                texpacker_atlas_rect_desc_t desc;
                desc.r = _r;
                desc.rotate = 1;

                _nr[(*_count)++] = desc;
            }
            else
            {
                return 0;
            }
        }break;
    case 1:
        {
            uint32_t begin_index = 0;
            uint32_t end_index = 0;

            switch( _r->state & 0x00000F00 )
            {
            case 0x00000000:
                {
                    begin_index = 0;
                    end_index = 4;
                }break;
            case 0x00000100:
                {
                    begin_index = 0;
                    end_index = 2;
                }break;
            case 0x00000200:
                {
                    begin_index = 2;
                    end_index = 4;
                }break;
            }

            for( uint32_t index = begin_index; index != end_index; ++index )
            {
                texpacker_atlas_rect_t * rl = _r->l[index];

                if( texpacker_find_atlas_rects( rl, _t, _nr, _count ) != 0 )
                {
                    return 1;
                }
            }
        }break;
    case 2:
        {
            //dummy

            return 0;
        }break;
    default:
        return 1;
        break;
    }

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __new_pow2( uint32_t x )
{
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x++;

    return x;
}
//////////////////////////////////////////////////////////////////////////
static void texpacker_get_texture_bounds_pow2( const texpacker_in_data_t * const _data, uint32_t * const _width, uint32_t * const _height )
{
    uint32_t max_width = 0;
    uint32_t max_height = 0;

    for( uint32_t index = 0; index != _data->textures_count; ++index )
    {
        const texpacker_texture_t * t = _data->textures + index;

        max_width = t->width > max_width ? t->width : max_width;
        max_height = t->height > max_height ? t->height : max_height;
    }

    *_width = __new_pow2( max_width );
    *_height = __new_pow2( max_height );
}
//////////////////////////////////////////////////////////////////////////
static int texpacker_probe_atlas_rect( uint32_t _width, uint32_t _height, const texpacker_in_data_t * const _data, texpacker_atlas_rect_t ** _rect, uint32_t * const _packaged, uint32_t * const _unpackaged )
{
    texpacker_atlas_rect_t * rect = texpacker_make_atlas_rect( 0, 0, _width, _height );

    uint32_t packaged = 0;
    uint32_t unpackaged = 0;

    for( uint32_t index = 0; index != _data->textures_count; ++index )
    {
        texpacker_texture_t * t = _data->textures + index;

        if( t->atlas != NULL )
        {
            continue;
        }

        if( t->atlas_rect != NULL )
        {
            continue;
        }

        texpacker_atlas_rect_desc_t nr[2048] = {NULL};
        uint32_t nr_count = 0;
        if( texpacker_find_atlas_rects( rect, t, nr, &nr_count ) != 0 )
        {
            return 1;
        }

        uint32_t density = ~0U;
        const texpacker_atlas_rect_desc_t * df = NULL;

        for( uint32_t nr_index = 0; nr_index != nr_count; ++nr_index )
        {
            const texpacker_atlas_rect_desc_t * d = nr + nr_index;

            const texpacker_atlas_rect_t * r = d->r;
            int8_t rotate = d->rotate;

            uint32_t tw = rotate == 0 ? t->width : t->height;
            uint32_t th = rotate == 0 ? t->height : t->width;

            uint32_t dw = r->w - tw;
            uint32_t dh = r->h - th;
            uint32_t dwh = dw * th + dh * tw + dw * dh;

            if( dwh < density )
            {
                density = dwh;
                df = d;
            }
        }

        if( df == NULL )
        {
            ++unpackaged;

            continue;
        }

        ++packaged;

        printf( "texture: %ls density %u\n", t->path, density );

        texpacker_atlas_rect_t * rf = df->r;
        int8_t rotatef = df->rotate;

        if( texpacker_fill_atlas_rect( rf, rotatef, t ) != 0 )
        {
            return 1;
        }

        if( texpacker_mark_atlas_rect( rect, rf ) != 0 )
        {
            return 1;
        }

        t->atlas_rect = rf;
    }

    *_rect = rect;
    *_packaged = packaged;
    *_unpackaged = unpackaged;

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void texpacker_render_atlas_border( texpacker_atlas_t * _atlas, const texpacker_texture_t * _texture, uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _a )
{
    uint32_t x = _texture->atlas_rect->x;
    uint32_t y = _texture->atlas_rect->y;

    uint32_t u = _texture->atlas_rect->u;
    uint32_t v = _texture->atlas_rect->v;

    uint32_t width = _atlas->width;

    uint8_t * atlas_pixels_byte = (uint8_t *)_atlas->pixels;
    uint32_t atlas_pixel_size = _atlas->channel;

    uint32_t pitch = width * atlas_pixel_size;

    uint8_t rgba[] = {_r, _g, _b, _a};

    for( uint32_t index = 0; index != u; ++index )
    {
        memcpy( atlas_pixels_byte + x * atlas_pixel_size + y * pitch + index * atlas_pixel_size, rgba, atlas_pixel_size );
    }

    for( uint32_t index = 0; index != u; ++index )
    {
        memcpy( atlas_pixels_byte + x * atlas_pixel_size + y * pitch + index * atlas_pixel_size + (v - 1) * pitch, rgba, atlas_pixel_size );
    }

    for( uint32_t index = 0; index != v; ++index )
    {
        memcpy( atlas_pixels_byte + x * atlas_pixel_size + y * pitch + index * pitch, rgba, atlas_pixel_size );
    }

    for( uint32_t index = 0; index != v; ++index )
    {
        memcpy( atlas_pixels_byte + x * atlas_pixel_size + y * pitch + index * pitch + (u - 1) * atlas_pixel_size, rgba, atlas_pixel_size );
    }
}
//////////////////////////////////////////////////////////////////////////
static int texpacker_make_atlas( const texpacker_in_data_t * const _data, texpacker_atlas_t ** const _atlas, uint32_t * const _packaged, uint32_t * const _unpackaged )
{
    if( _data->textures_count == 0 )
    {
        return 0;
    }

    uint32_t base_max_width;
    uint32_t base_max_height;
    texpacker_get_texture_bounds_pow2( _data, &base_max_width, &base_max_height );

    if( base_max_width > _data->atlas_max_width || base_max_height > _data->atlas_max_height )
    {
        return 1;
    }

    texpacker_atlas_rect_t * r0 = NULL;
    uint32_t packaged = 0;
    uint32_t unpackaged = 0;

    uint32_t probe_width[] = {0, 1, 0, 1, 2, 1, 2, 3, 2, 3, 4, 3, 4, 5, 4, 5, 6, 5, 6, 7, 6, 7, 8, 7, 8, 9, 8, 9, 10, 9, 10, 9};
    uint32_t probe_height[] = {0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10};

    for( uint32_t atlas_probe = 0; atlas_probe != sizeof( probe_width ) / sizeof( uint32_t ); ++atlas_probe )
    {
        uint32_t probe_atlas_width = base_max_width << probe_width[atlas_probe];
        uint32_t probe_atlas_height = base_max_height << probe_height[atlas_probe];

        if( probe_atlas_width > _data->atlas_max_width || probe_atlas_height > _data->atlas_max_height )
        {
            continue;
        }

        for( uint32_t index = 0; index != _data->textures_count; ++index )
        {
            texpacker_texture_t * t = _data->textures + index;

            if( t->atlas != NULL )
            {
                continue;
            }

            t->atlas_rect = NULL;
        }

        if( texpacker_probe_atlas_rect( probe_atlas_width, probe_atlas_height, _data, &r0, &packaged, &unpackaged ) != 0 )
        {
            return 1;
        }

        if( unpackaged == 0 )
        {
            break;
        }
    }

    texpacker_atlas_t * atlas = TEXPACKER_NEW( texpacker_atlas_t );

    atlas->width = r0->w;
    atlas->height = r0->h;
    atlas->channel = _data->atlas_channels;

    size_t atlas_pixels_size = atlas->width * atlas->height * atlas->channel;
    void * atlas_pixels = malloc( atlas->width * atlas->height * atlas->channel );

    memset( atlas_pixels, 0x00, atlas_pixels_size );

    atlas->pixels = atlas_pixels;

    uint8_t * altas_pixels_byte = (uint8_t *)atlas->pixels;
    uint32_t atlas_pixel_size = atlas->channel;

    for( uint32_t texture_index = 0; texture_index != _data->textures_count; ++texture_index )
    {
        texpacker_texture_t * texture = _data->textures + texture_index;

        if( texture->atlas != NULL )
        {
            continue;
        }

        texpacker_atlas_rect_t * atlas_rect = texture->atlas_rect;

        if( atlas_rect == NULL )
        {
            continue;
        }

        uint32_t bx = atlas_rect->x;
        uint32_t by = atlas_rect->y;

        uint8_t * texture_pixels_byte = (uint8_t *)texture->pixels;
        uint32_t texture_pixel_size = sizeof( uint8_t ) * texture->channel;
        uint32_t texture_row_size = texture->width * texture_pixel_size;

        if( atlas_rect->rotate == 0 )
        {
            uint32_t u = atlas_rect->u;
            uint32_t v = atlas_rect->v;

            if( atlas_pixel_size == 4 && texture_pixel_size == 4 )
            {
                for( uint32_t v_index = 0; v_index != v; ++v_index )
                {
                    memcpy( altas_pixels_byte + (bx + (by + v_index) * atlas->width) * atlas_pixel_size, texture_pixels_byte + v_index * texture_row_size, texture_row_size );
                }
            }
            else if( atlas_pixel_size == 4 && texture_pixel_size == 3 )
            {
                for( uint32_t u_index = 0; u_index != u; ++u_index )
                {
                    for( uint32_t v_index = 0; v_index != v; ++v_index )
                    {
                        uint32_t atlas_offset = (bx + u_index + (by + v_index) * atlas->width) * atlas_pixel_size;
                        uint32_t texture_offset = (u_index + v_index * texture->width) * texture_pixel_size;

                        *(altas_pixels_byte + atlas_offset + 0) = *(texture_pixels_byte + texture_offset + 0);
                        *(altas_pixels_byte + atlas_offset + 1) = *(texture_pixels_byte + texture_offset + 1);
                        *(altas_pixels_byte + atlas_offset + 2) = *(texture_pixels_byte + texture_offset + 2);
                        *(altas_pixels_byte + atlas_offset + 3) = 255;
                    }
                }
            }

            texpacker_render_atlas_border( atlas, texture, 255, 0, 0, 255 );
        }
        else
        {
            uint32_t u = atlas_rect->u;
            uint32_t v = atlas_rect->v;

            if( atlas_pixel_size == 4 && texture_pixel_size == 4 )
            {
                for( uint32_t u_index = 0; u_index != u; ++u_index )
                {
                    for( uint32_t v_index = 0; v_index != v; ++v_index )
                    {
                        uint32_t atlas_offset = (bx + u_index + (by + v_index) * atlas->width) * atlas_pixel_size;
                        uint32_t texture_offset = (v_index + u_index * texture->width) * texture_pixel_size;

                        *(altas_pixels_byte + atlas_offset + 0) = *(texture_pixels_byte + texture_offset + 0);
                        *(altas_pixels_byte + atlas_offset + 1) = *(texture_pixels_byte + texture_offset + 1);
                        *(altas_pixels_byte + atlas_offset + 2) = *(texture_pixels_byte + texture_offset + 2);
                        *(altas_pixels_byte + atlas_offset + 3) = *(texture_pixels_byte + texture_offset + 3);
                    }
                }
            }
            else if( atlas_pixel_size == 4 && texture_pixel_size == 3 )
            {
                for( uint32_t u_index = 0; u_index != u; ++u_index )
                {
                    for( uint32_t v_index = 0; v_index != v; ++v_index )
                    {
                        uint32_t atlas_offset = (bx + u_index + (by + v_index) * atlas->width) * atlas_pixel_size;
                        uint32_t texture_offset = (v_index + u_index * texture->width) * texture_pixel_size;

                        *(altas_pixels_byte + atlas_offset + 0) = *(texture_pixels_byte + texture_offset + 0);
                        *(altas_pixels_byte + atlas_offset + 1) = *(texture_pixels_byte + texture_offset + 1);
                        *(altas_pixels_byte + atlas_offset + 2) = *(texture_pixels_byte + texture_offset + 2);
                        *(altas_pixels_byte + atlas_offset + 3) = 255;
                    }
                }
            }

            texpacker_render_atlas_border( atlas, texture, 0, 255, 0, 255 );
        }

        texture->atlas = atlas;
    }

    *_atlas = atlas;
    *_packaged = packaged;
    *_unpackaged = unpackaged;

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void __texpacker_stbi_write( void * _context, void * _data, int _size )
{
    FILE * f = (FILE *)_context;

    fwrite( _data, _size, 1, f );
}
//////////////////////////////////////////////////////////////////////////
static int texpacker_save_atlas( texpacker_in_data_t * const _data, texpacker_atlas_t * _atlas, uint32_t _index )
{
    wchar_t output_path[FILENAME_MAX];

    if( _index == 0 )
    {
        wcscpy( output_path, _data->output_atlas_path );
    }
    else
    {
        const wchar_t * output_path_format = (_data->output_atlas_path_format != NULL) ? _data->output_atlas_path_format : L"%.*s_%02u%s";
        
        swprintf( output_path, FILENAME_MAX, output_path_format, _data->output_atlas_path_ext - _data->output_atlas_path, _data->output_atlas_path, _index, _data->output_atlas_path_ext );
    }

    FILE * f = _wfopen( output_path, L"wb" );

    if( f == NULL )
    {
        return 1;
    }

    int stbi_result = stbi_write_png_to_func( &__texpacker_stbi_write, f, _atlas->width, _atlas->height, _atlas->channel, _atlas->pixels, _atlas->width * _atlas->channel );

    fclose( f );

    if( stbi_result == 0 )
    {
        return 1;
    }

    _atlas->index = _index;
    wcscpy( _atlas->path, output_path );

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int texpacker_save_atlas_info( texpacker_in_data_t * const _data, texpacker_atlas_t ** const _atlases, uint32_t _atlases_count )
{
    json_t * j = json_object();

    json_t * j_atlases = json_array();

    for( uint32_t i = 0; i != _atlases_count; ++i )
    {
        texpacker_atlas_t * atlas = _atlases[i];

        json_t * j_atlas = json_object();

        char mbstr_atlas_path[FILENAME_MAX];
        wcstombs( mbstr_atlas_path, atlas->path, FILENAME_MAX );

        json_object_set_new( j_atlas, "path", json_string( mbstr_atlas_path ) );
        json_object_set_new( j_atlas, "w", json_integer( atlas->width ) );
        json_object_set_new( j_atlas, "h", json_integer( atlas->height ) );

        json_array_append_new( j_atlases, j_atlas );
    }

    json_object_set_new( j, "atlases", j_atlases );

    json_t * j_textures = json_array();

    uint32_t textures_count = _data->textures_count;

    for( uint32_t i = 0; i != textures_count; ++i )
    {
        const texpacker_texture_t * texture = _data->textures + i;

        json_t * j_texture = json_object();

        char mbstr_texture_path[FILENAME_MAX];
        wcstombs( mbstr_texture_path, texture->path, FILENAME_MAX );

        json_object_set_new( j_texture, "path", json_string( mbstr_texture_path ) );
        json_object_set_new( j_texture, "atlas", json_integer( texture->atlas->index ) );

        float atlas_width_inv = 1.f / (float)texture->atlas->width;
        float atlas_height_inv = 1.f / (float)texture->atlas->height;

        float uv_x = (float)texture->atlas_rect->x * atlas_width_inv;
        float uv_y = (float)texture->atlas_rect->y * atlas_height_inv;
        float uv_u = (float)texture->atlas_rect->u * atlas_width_inv;
        float uv_v = (float)texture->atlas_rect->v * atlas_height_inv;

        json_object_set_new( j_texture, "x", json_real( uv_x ) );
        json_object_set_new( j_texture, "y", json_real( uv_y ) );
        json_object_set_new( j_texture, "u", json_real( uv_u ) );
        json_object_set_new( j_texture, "v", json_real( uv_v ) );

        if( texture->atlas_rect->rotate == 1 )
        {
            json_object_set_new( j_texture, "rotate", json_true() );
        }

        json_array_append_new( j_textures, j_texture );
    }

    json_object_set_new( j, "textures", j_textures );
    
    FILE * f = _wfopen( _data->output_atlas_info, L"wb" );

    if( f == NULL )
    {
        return 1;
    }

    int res = json_dumpf( j, f, JSON_INDENT( 2 ) );

    json_decref( j );

    fclose( f );

    if( res != 0 )
    {
        return 1;
    }

    return 0;
}
//////////////////////////////////////////////////////////////////////////
int wmain( int argc, wchar_t * argv[] )
{
    if( argc != 2 )
    {
        return EXIT_FAILURE;
    }

    const wchar_t * data_path = argv[1];

    void * data_buffer;
    size_t data_len;
    if( texpacker_load_data_buffer( data_path, &data_buffer, &data_len ) != 0 )
    {
        return EXIT_FAILURE;
    }

    texpacker_in_data_t in_data;

    if( texpacker_load_in_data( data_buffer, data_len, &in_data ) != 0 )
    {
        free( data_buffer );

        return EXIT_FAILURE;
    }

    free( data_buffer );

    if( texpacker_load_texures_pixels( &in_data ) != 0 )
    {
        return EXIT_FAILURE;
    }

    if( texpacker_load_texures_sort( &in_data ) != 0 )
    {
        return EXIT_FAILURE;
    }

    texpacker_atlas_t * atlases[256];
    uint32_t atlases_count = 0;
    for( uint32_t index = 0;; ++index )
    {
        if( atlases_count == 256 )
        {
            return EXIT_FAILURE;
        }

        texpacker_atlas_t * atlas;
        uint32_t packaged;
        uint32_t unpackaged;
        if( texpacker_make_atlas( &in_data, &atlas, &packaged, &unpackaged ) != 0 )
        {
            return EXIT_FAILURE;
        }

        if( texpacker_save_atlas( &in_data, atlas, index ) != 0 )
        {
            return EXIT_FAILURE;
        }

        atlases[index] = atlas;
        ++atlases_count;

        if( unpackaged == 0 )
        {
            break;
        }
    }

    for( uint32_t i = 0; i != in_data.textures_count; ++i )
    {
        const texpacker_texture_t * texture = in_data.textures + i;

        if( texture->atlas == NULL )
        {
            return EXIT_FAILURE;
        }
    }

    if( texpacker_save_atlas_info( &in_data, atlases, atlases_count ) != 0 )
    {
        return EXIT_FAILURE;
    }

    for( uint32_t i = 0; i != in_data.textures_count; ++i )
    {
        const texpacker_texture_t * texture = in_data.textures + i;

        printf( "texture: %ls atlas %ls uv %u %u\n", texture->path, texture->atlas->path, texture->atlas_rect->x, texture->atlas_rect->y );
    }

    for( uint32_t i = 0; i != in_data.textures_count; ++i )
    {
        const texpacker_texture_t * texture = in_data.textures + i;

        const wchar_t * path = texture->path;

        free( (void *)path );
    }

    free( (void *)in_data.textures );

    return EXIT_SUCCESS;
}
//////////////////////////////////////////////////////////////////////////