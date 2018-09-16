/*
 * Image.cc
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013 ZhangYuanwei <zhangyuanwei1988@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL INTEL AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "Image.h"
#include "Resize.h"
#include "Rotate.h"
#include <node_buffer.h>
#include <node_api.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <iostream>
#include <assert.h>

using v8::Exception;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::Persistent;
using v8::PropertyCallbackInfo;
using v8::String;
using v8::Value;

//#define SET_ERROR_FILE_LINE(file, line, msg) Image::SetError( file #line msg)
//#define SET_ERROR(msg) SET_ERROR_FILE_LINE(__FILE__, __LINE__, meg)

#define STRINGFY(n) #n
#define MERGE_FILE_LINE(file, line, msg) (file ":" STRINGFY(line) " " msg)
#define FILE_LINE(msg) MERGE_FILE_LINE(__FILE__, __LINE__, msg)
#define ERROR(type, msg) Exception::type(String::NewFromUtf8(Isolate::GetCurrent(), msg))
#define THROW(err) Isolate::GetCurrent()->ThrowException(err)
#define SET_ERROR(msg) (Image::setError(FILE_LINE(msg)))
#define GET_ERROR() (Image::getError())
#define THROW_ERROR(msg) THROW(ERROR(Error, FILE_LINE(msg)))
#define THROW_GET_ERROR() THROW(GET_ERROR())

#define THROW_TYPE_ERROR(msg) THROW(ERROR(TypeError, FILE_LINE(msg)))
#define THROW_INVALID_ARGUMENTS_ERROR(msg) THROW_TYPE_ERROR("Invalid arguments" msg)

#define DEFAULT_WIDTH_LIMIT 10240  // default limit 10000x10000
#define DEFAULT_HEIGHT_LIMIT 10240 // default limit 10000x10000

#define AdjustAmountOfExternalAllocatedMemory(bc) static_cast<int>( \
    v8::Isolate::GetCurrent()->AdjustAmountOfExternalAllocatedMemory(bc));

napi_ref Image::constructor;

//size_t Image::survival;
ImageCodec *Image::codecs;

size_t Image::maxWidth = DEFAULT_WIDTH_LIMIT;
size_t Image::maxHeight = DEFAULT_HEIGHT_LIMIT;
const char *Image::error = NULL;

#define DECLARE_NAPI_METHOD(name, func)         \
    {                                           \
        name, 0, func, 0, 0, 0, napi_default, 0 \
    }

napi_value Image::Init(napi_env env, napi_value exports)
{ // {{{
    Isolate *isolate = exports->GetIsolate();
    regAllCodecs();
    //survival = 0;

    // Constructor
    Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
    tpl->SetClassName(String::NewFromUtf8(isolate, "Image"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    // Prototype
    Local<ObjectTemplate> proto = tpl->PrototypeTemplate();

    //define prototype method
    NODE_SET_PROTOTYPE_METHOD(tpl, "resize", Resize);
    NODE_SET_PROTOTYPE_METHOD(tpl, "rotate", Rotate);
    NODE_SET_PROTOTYPE_METHOD(tpl, "fillColor", FillColor);
    NODE_SET_PROTOTYPE_METHOD(tpl, "loadFromBuffer", LoadFromBuffer);
    NODE_SET_PROTOTYPE_METHOD(tpl, "copyFromImage", CopyFromImage);
    NODE_SET_PROTOTYPE_METHOD(tpl, "drawImage", DrawImage);
    NODE_SET_PROTOTYPE_METHOD(tpl, "toBuffer", ToBuffer);

    //define setter and getter method
    proto->SetAccessor(String::NewFromUtf8(isolate, "width"), GetWidth, SetWidth);
    proto->SetAccessor(String::NewFromUtf8(isolate, "height"), GetHeight, SetHeight);
    proto->SetAccessor(String::NewFromUtf8(isolate, "transparent"), GetTransparent);

    constructor.Reset(isolate, tpl->GetFunction());

    NODE_DEFINE_CONSTANT(exports, TYPE_PNG);
    NODE_DEFINE_CONSTANT(exports, TYPE_JPEG);
    NODE_DEFINE_CONSTANT(exports, TYPE_GIF);
    NODE_DEFINE_CONSTANT(exports, TYPE_BMP);
    NODE_DEFINE_CONSTANT(exports, TYPE_RAW);
    NODE_DEFINE_CONSTANT(exports, TYPE_WEBP);

    exports->SetAccessor(String::NewFromUtf8(isolate, "maxWidth"), GetMaxWidth, SetMaxWidth);
    exports->SetAccessor(String::NewFromUtf8(isolate, "maxHeight"), GetMaxHeight, SetMaxHeight);
    exports->SetAccessor(String::NewFromUtf8(isolate, "usedMemory"), GetUsedMemory);
    NODE_SET_METHOD(exports, "gc", GC);

    napi_property_descriptor imageDescriptor = DECLARE_NAPI_METHOD("Image", Image);
    status = napi_define_properties(env, exports, 1, &imageDescriptor);
    assert(status == napi_ok);
    exports->Set(String::NewFromUtf8(isolate, "Image"), tpl->GetFunction());

} //}}}

ImageState Image::setError(const char *err)
{ // {{{
    error = err;
    return FAIL;
} // }}}

napi_value Image::getError()
{ // {{{
    napi_value err = Exception::Error(String::NewFromUtf8(Isolate::GetCurrent(), error ? error : "Unknow Error"));
    error = NULL;
    return err;
} // }}}

bool Image::isError()
{ // {{{
    return error != NULL;
} // }}}

napi_value Image::GetMaxWidth(Local<String> property, const PropertyCallbackInfo<Value> &args)
{ // {{{
    Isolate *isolate = args.GetIsolate();
    args.GetReturnValue().Set(Number::New(isolate, maxWidth));

} // }}}

napi_value Image::SetMaxWidth(Local<String> property, Local<Value> value, const PropertyCallbackInfo<void> &args)
{ // {{{
    if (value->IsNumber())
        maxWidth = value->Uint32Value();
} // }}}

napi_value Image::GetMaxHeight(Local<String> property, const PropertyCallbackInfo<Value> &args)
{ // {{{

} // }}}

napi_value Image::SetMaxHeight(Local<String> property, Local<Value> value, const PropertyCallbackInfo<void> &args)
{ // {{{
    if (value->IsNumber())
        maxHeight = value->Uint32Value();
} // }}}

// Memory
size_t Image::usedMemory = 0;

napi_value Image::GetUsedMemory(Local<String> property, const PropertyCallbackInfo<Value> &args)
{ // {{{
    Isolate *isolate = args.GetIsolate();
    args.GetReturnValue().Set(Number::New(isolate, usedMemory));
} // }}}

napi_value Image::GC(napi_env env, const napi_callback_info info)
{ // {{{
    //V8::LowMemoryNotification();
    // Isolate *isolate = args.GetIsolate();
    // args.GetReturnValue().Set(v8::Undefined(isolate));
    napi_status status;
    size_t argc = 1;
    napi_value argv[1];
    status = napi_get_cb_info(env, args, &argc, argv, 0, 0);
    if (status != napi_ok)
    {
        std::cout << argv[0];
    }
    std::cout << argv[0];
} // }}}

napi_value Image::New(const FunctionCallbackInfo<Value> &args)
{ // {{{

    Image *img;

    size_t width, height;

    width = height = 0;

    if (args[0]->IsNumber())
        width = args[0]->Uint32Value();
    if (args[1]->IsNumber())
        height = args[1]->Uint32Value();

    img = new Image();

    if (img->pixels->Malloc(width, height) != SUCCESS)
    {
        THROW_GET_ERROR();
    }

    img->Wrap(args.This());

    args.GetReturnValue().Set(args.This());
} // }}}

napi_value Image::GetWidth(Local<String> property, const PropertyCallbackInfo<Value> &args)
{ // {{{
    Image *img = node::ObjectWrap::Unwrap<Image>(args.This());
    Isolate *isolate = args.GetIsolate();
    args.GetReturnValue().Set(Number::New(isolate, img->pixels->width));
} // }}}

napi_value Image::SetWidth(Local<String> property, Local<Value> value, const PropertyCallbackInfo<void> &args)
{ // {{{
    if (value->IsNumber())
    {
        Image *img = node::ObjectWrap::Unwrap<Image>(args.This());
        img->pixels->SetWidth(value->Uint32Value());
    }
} // }}}

napi_value Image::GetHeight(Local<String> property, const PropertyCallbackInfo<Value> &args)
{ // {{{

    Image *img = node::ObjectWrap::Unwrap<Image>(args.This());
    args.GetReturnValue().Set(Number::New(args.GetIsolate(), img->pixels->height));

} // }}}

napi_value Image::SetHeight(Local<String> property, Local<Value> value, const PropertyCallbackInfo<void> &args)
{ // {{{
    if (value->IsNumber())
    {
        Image *img = node::ObjectWrap::Unwrap<Image>(args.This());
        img->pixels->SetHeight(value->Uint32Value());
    }
} // }}}

/**
 * Scale image with bicubic.
 * @since 1.5.5+
 */
napi_value Image::Resize(napi_env env, const napi_callback_info info)
{
    napi_status status;
    napi_value jsthis;

    char *filter = NULL;

    if ((!args[0]->IsNull() && !args[0]->IsUndefined() && !args[0]->IsNumber()) ||
        (!args[1]->IsNull() && !args[1]->IsUndefined() && !args[1]->IsNumber()))
    {
        THROW_INVALID_ARGUMENTS_ERROR("Arguments error");
        return;
    }

    if (args[2]->IsString())
    {
        String::Utf8Value cstr(args[2]);
        filter = new char[strlen(*cstr) + 1];
        strcpy(filter, *cstr);
    }

    Image *img = node::ObjectWrap::Unwrap<Image>(args.This());
    img->pixels->Resize(args[0]->ToNumber()->Value(), args[1]->ToNumber()->Value(), filter);

    args.GetReturnValue().Set(v8::Undefined(args.GetIsolate()));
}

/**
 * Rotate image.
 * @since 1.5.5+
 */
napi_value Image::Rotate(const FunctionCallbackInfo<Value> &args)
{

    char *filter = NULL;

    if (!args[0]->IsNull() && !args[0]->IsUndefined() && !args[0]->IsNumber())
    {
        THROW_INVALID_ARGUMENTS_ERROR("Arguments error");
        return;
    }

    Image *img = node::ObjectWrap::Unwrap<Image>(args.This());
    img->pixels->Rotate(args[0]->ToNumber()->Value());

    args.GetReturnValue().Set(v8::Undefined(args.GetIsolate()));
}

napi_value Image::GetTransparent(Local<String> property, const PropertyCallbackInfo<Value> &args)
{ // {{{
    Image *img = node::ObjectWrap::Unwrap<Image>(args.This());
    args.GetReturnValue().Set(Number::New(args.GetIsolate(), img->pixels->type));
} // }}}

napi_value Image::FillColor(napi_env env, napi_callback_info info)
{ // {{{
    napi_status status;
    size_t argc = 4;
    napi_value args[4];
    status = napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    assert(status == napi_ok);

    // if (argc < 3)
    // {
    //     napi_throw_type_error(env, nullptr, 'Wrong number of arguments');
    //     return nullptr;
    // }

    napi_valuetype valuetype0;
    status = napi_typeof(env, args[0], &valuetype0);
    assert(status == napi_ok);

    napi_valuetype valuetype1;
    status = napi_typeof(env, args[1], &valuetype1);
    assert(status == napi_ok);

    napi_valuetype valuetype2;
    status = napi_typeof(env, args[2], &valuetype2);
    assert(status == napi_ok);

    napi_valuetype valuetype3;
    status = napi_typeof(env, args[3], &valuetype3);
    assert(status == napi_ok);

    Image *img;
    Pixel color, *cp;

    if (valuetype0 != napi_number || valuetype1 != napi_number || valuetype2 != napi_number)
    {
        napi_throw_type_error(env, nullptr, "wrong arguments");
        return nullptr;
    }

    uint32_t value0;
    status = napi_get_value_uint32(env, args[0], &value0);
    assert(status == napi_ok);

    uint32_t value1;
    status = napi_get_value_uint32(env, args[1], &value1);
    assert(status == napi_ok);

    uint32_t value2;
    status = napi_get_value_uint32(env, args[2], &value2);
    assert(status == napi_ok);

    uint32_t value3;
    status = napi_get_value_uint32(env, args[3], &value3);
    assert(status == napi_ok);

    cp = &color;
    cp->R = value0;
    cp->G = value1;
    cp->B = value2;
    cp->A = 0xFF;

    if (valuetype3 == napi_number)
    {
        cp->A = (uint8_t)(value3 * 0xFF);
    }
    status = napi_unwrap(env, args.This(), reinterpret_cast<void **>(&img));
    assert(status == napi_ok);
    img->pixels->Fill(cp);
    return napi_ok;
    // args.GetReturnValue().Set(v8::Undefined(args.GetIsolate()));
} // }}}

napi_value Image::LoadFromBuffer(napi_env env, const napi_callback_info info)
{ // {{{
    napi_status status;
    size_t argc = 3;
    napi_value args[3];
    status napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    assert(status == napi_ok);

    Image *img;

    uint8_t *buffer;
    unsigned start, end, length;

    ImageCodec *codec;
    ImageDecoder decoder;
    ImageData input_data, *input;

    if (!node::Buffer::HasInstance(args[0]))
    {
        THROW_TYPE_ERROR(": first argument must be a buffer.");
        return;
    }

    img = node::ObjectWrap::Unwrap<Image>(args.This());

    buffer = (uint8_t *)node::Buffer::Data(args[0]);
    length = (unsigned)node::Buffer::Length(args[0]);

    start = 0;
    if (args[1]->IsNumber())
    {
        start = args[1]->Uint32Value();
    }

    end = length;
    if (args[2]->IsNumber())
    {
        end = args[2]->Uint32Value();
        if (end < start || end > length)
        {
            THROW_TYPE_ERROR("");
            return;
        }
    }

    input = &input_data;
    input->data = &buffer[start];
    input->length = end - start;

    img->pixels->Free();
    codec = codecs;
    while (codec != NULL && !isError())
    {
        decoder = codec->decoder;
        input->position = 0;
        if (decoder != NULL && decoder(img->pixels, input) == SUCCESS)
        {
            args.GetReturnValue().Set(v8::Undefined(args.GetIsolate()));
            return;
        }
        codec = codec->next;
    }
    isError() ? (THROW_GET_ERROR()) : THROW_ERROR("Unknow format");
    return;
} // }}}

napi_value Image::CopyFromImage(napi_env env, const napi_get_cb_info info)
{ // {{{
    napi_env env;
    size_t argc;
    napi_value *args;
    napi_status status;

    status = napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    assert(status == napi_ok);

    Image *src, *dst;
    uint32_t x, y, w, h;

    Local<Object> obj = args[0]->ToObject();

    //@TODO
    status = napi_unwrap(env, args[0], reinterpret_cast<void **>(&src));
    assert(status == napi_ok);
    status = napi_unwrap(env, args.This(), reinterpret_cast<void **>(&dst));
    assert(status == napi_ok);
    // src = node::ObjectWrap::Unwrap<Image>(obj);
    // dst = node::ObjectWrap::Unwrap<Image>(args.This());

    x = y = 0;
    w = src->pixels->width;
    h = src->pixels->height;

    napi_valuetype valueType1;
    status = napi_typeof(env, args[1], &valueType1);
    assert(status == napi_ok);

    napi_valuetype valueType2;
    status = napi_typeof(env, args[2], &valueType2);
    assert(status == napi_ok);

    napi_valuetype valueType3;
    status = napi_typeof(env, args[3], &valueType3);
    assert(status == napi_ok);

    napi_valuetype valueType4;
    status = napi_typeof(env, args[4], &valueType4);
    assert(status == napi_ok);

    if (valueType1 == napi_number // x
        && valueType2 == napi_number)
    { // y
        uint32_t value1;
        status = napi_get_value_uint32(env, args[1], &value1);
        assert(status == napi_ok);

        uint32_t value2;
        status = napi_get_value_uint32(env, args[2], &value2);
        assert(status == napi_ok);

        x = value1;
        y = value2;
        // x = args[1]->Uint32Value();
        // y = args[2]->Uint32Value();
    }

    if (valueType3 == napi_number // w
        && valueType4 == napi_number)
    { // h
        uint32_t value3;
        status = napi_get_value_uint32(env, args[3], &value3);
        assert(status == napi_ok);

        uint32_t value4;
        status = napi_get_value_uint32(env, args[4], &value4);
        assert(status == napi_ok);

        w = value3;
        h = value4;
        // w = args[3]->Uint32Value();
        // h = args[4]->Uint32Value();
    }

    if (dst->pixels->CopyFrom(src->pixels, x, y, w, h) != SUCCESS)
    {
        THROW_GET_ERROR();
    }
    return npai_ok;
} // }}}

napi_value Image::DrawImage(npai_env env, const napi_callback_info info)
{ // {{{
    napi_env env;
    napi_status status;
    size_t argc = 3;
    napi_value args[3];

    Image *src, *dst;
    size_t x, y;

    status = napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    assert(status == napi_ok);

    // napi_value obj;
    // status = napi_create_Object(env, args[0]);
    Local<Object> obj = args[0]->ToObject();

    //if(!NanHasInstance(Image::constructor, obj)
    //   || !args[1]->IsNumber() // x
    //   || !args[2]->IsNumber()) // y

    napi_valuetype valuetype1;
    status = napi_typeof(env, args[1], &valuetype1);
    assert(status == napi_ok);

    napi_valuetype valuetype2;
    status = napi_typeof(env, args[2], &valuetype2);
    assert(status == napi_ok);

    status = napi_get_value_uint32(env, args[1], &value1);
    assert(status == napi_ok);

    if (valuetype1 != napi_number || valuetype2 != napi_number)
    {
        napi_throw_type_error(env, nullptr, "Invalid Arguments");
        return;
    }

    status = napi_unwrap(env, args[0], reinterpret_cast<void **>(&src));
    assert(status, napi_ok);
    status = napi_unwrap(env, args.This(), reinterpret_cast<void **>(&dst));
    assert(status, napi_ok);

    status = napi_get_value_uint32(env, args[1], &x);
    assert(status == napi_ok);
    status = napi_get_value_uint32(env, args[2], &y);
    assert(status == napi_ok);

    dst->pixels->Draw(src->pixels, x, y);

    //args.GetReturnValue().Set(v8::Undefined(args.GetIsolate()));
    return napi_ok;
} // }}}

napi_value Image::ToBuffer(npai_env env, const napi_callback_info info)
{ //{{{
    napi_status status;
    size_t argc = 2;
    napi_value args[2];
    status = napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    assert(status == napi_ok);

    Image *img;
    ImageType type;
    PixelArray *pixels;
    ImageConfig _config, *config;
    ImageCodec *codec;
    ImageEncoder encoder;

    ImageData output_data, *output;

    //Local<Object> buffer;
    napi_value buffer;
    int length;

    napi_valuetype valuetype0;
    status = napi_typeof(env, args[0], &valuetype0);
    assert(status == napi_ok);

    if (valuetype0 != napi_number)
    {
        napi_throw_type_error(env, nullptr, "Invalid Arguments");
        return;
    }

    uint32_t value0;
    status = napi_get_value_uint32(env, args[0], &value0);
    type = (ImageType)value0;
    config = NULL;

    bool bufferFlag;
    status = napi_is_buffer(env, args[1], &bufferFlag);
    assert(status == napi_ok);

    if (bufferFlag)
    {
        char *data;
        size_t len;
        status = napi_get_buffer_info(env, args[1], &data, &len);
        config = &_config;
        config->data = data;
        config->length = len;
    }

    //img = node::ObjectWrap::Unwrap<Image>(args.This());
    status = napi_unwrap(env, args.This(), reinterpret_cast<void **>(&img));
    assert(status == napi_ok);
    pixels = img->pixels;

    if (pixels->data != NULL)
    {
        codec = codecs;
        output = &output_data;
        output->data = NULL;
        output->length = 0;
        output->position = 0;

        while (codec != NULL && !isError())
        {
            if (codec->type == type)
            {
                encoder = codec->encoder;
                if (encoder != NULL)
                {
                    if (encoder(pixels, output, config) == SUCCESS)
                    {
                        length = output->position;
                        MaybeLocal<Object> maybeBuffer = node::Buffer::New(args.GetIsolate(), (size_t)length);
                        maybeBuffer.ToLocal(&buffer);
                        memcpy(node::Buffer::Data(buffer), output->data, length);
                        free(output->data);
                        //args.GetReturnValue().Set(buffer);
                        return buffer;
                    }
                    else
                    {
                        if (output->data != NULL)
                            free(output->data);
                        THROW_ERROR("Encode fail.");
                        return;
                    }
                }
                else
                {
                    THROW_ERROR("Can't encode to this format.");
                }
            }
            codec = codec->next;
        }
        isError() ? (THROW_GET_ERROR()) : (THROW_ERROR("Unsupported type."));
        return;
    }
    else
    {
        THROW_ERROR("Image uninitialized.");
        return;
    }

} // }}}

void Image::regCodec(ImageDecoder decoder, ImageEncoder encoder, ImageType type)
{ // {{{
    ImageCodec *codec;
    codec = (ImageCodec *)malloc(sizeof(ImageCodec));
    codec->next = codecs;
    codec->decoder = decoder;
    codec->encoder = encoder;
    codec->type = type;
    codecs = codec;
} // }}}

Image::Image()
{ // {{{
    size_t size;
    pixels = (PixelArray *)malloc(sizeof(PixelArray));
    pixels->width = pixels->height = 0;
    pixels->type = EMPTY;
    pixels->data = NULL;
    size = sizeof(PixelArray) + sizeof(Image);
    AdjustAmountOfExternalAllocatedMemory(size);
    usedMemory += size;
    //survival++;
} // }}}

Image::~Image()
{ // {{{
    int32_t size;
    size = sizeof(PixelArray) + sizeof(Image);
    pixels->Free();
    free(pixels);
    AdjustAmountOfExternalAllocatedMemory(-size);
    usedMemory -= size;
    //survival--;
    //printf("survival:%d\n", survival);
} // }}}

void Pixel::Merge(Pixel *pixel)
{ // {{{
    double a, af, ab;
    ab = (double)A / 0xFF;
    af = (double)pixel->A / 0xFF;
    a = (1 - (1 - af) * (1 - ab));

    R = (uint8_t)((pixel->R * af + R * ab * (1 - af)) / a);
    G = (uint8_t)((pixel->G * af + G * ab * (1 - af)) / a);
    B = (uint8_t)((pixel->B * af + B * ab * (1 - af)) / a);
    A = (uint8_t)(a * 0xFF);
} // }}}

ImageState PixelArray::Malloc(size_t w, size_t h)
{ // {{{
    int32_t size;
    Pixel *line;

    if (w > 0 && h > 0)
    {
        if (w > Image::maxWidth || h > Image::maxHeight)
        {
            SET_ERROR("Beyond the pixel size limit.");
            goto fail;
        }

        if ((data = (Pixel **)malloc(h * sizeof(Pixel **))) == NULL)
        {
            SET_ERROR("Out of memory.");
            goto fail;
        }

        width = w;
        size = width * sizeof(Pixel);
        for (height = 0; height < h; height++)
        {
            if ((line = (Pixel *)malloc(size)) == NULL)
            {
                SET_ERROR("Out of memory.");
                goto free;
            }
            memset(line, 0x00, size);
            data[height] = line;
        }
    }
    size = Size();
    AdjustAmountOfExternalAllocatedMemory(size);
    Image::usedMemory += size;
    return SUCCESS;

free:
    while (height--)
        free(data[height]);
    free(data);

fail:
    width = height = 0;
    type = EMPTY;
    data = NULL;
    return FAIL;
} // }}}

void PixelArray::Free()
{ // {{{
    size_t h, size;

    if (data != NULL)
    {
        h = height;
        while (h--)
        {
            if (data[h] != NULL)
                free(data[h]);
        }
        free(data);
        size = Size();
        AdjustAmountOfExternalAllocatedMemory(-size);
        Image::usedMemory -= size;
    }

    width = height = 0;
    type = EMPTY;
    ;
    data = NULL;
} // }}}

ImageState PixelArray::CopyFrom(PixelArray *src, size_t x, size_t y, size_t w, size_t h)
{ // {{{
    size_t sw, sh, size;

    sw = src->width;
    sh = src->height;

    if (src->data && x < sw && y < sh)
    {
        if (x + w > sw)
            w = sw - x;
        if (y + h > sh)
            h = sh - y;

        Free();
        size = w * sizeof(Pixel);
        if (Malloc(w, h) != SUCCESS)
            return FAIL;

        while (h--)
        {
            memcpy(data[h], &(src->data[y + h][x]), size);
        }
        type = src->type;
    }

    return SUCCESS;
} // }}}

void PixelArray::Draw(PixelArray *src, size_t x, size_t y)
{ // {{{
    //TODO
    size_t sw, sh, dw, dh, w, h, sx, sy, size;
    PixelArrayType st;
    Pixel *sp, *dp;

    sw = src->width;
    sh = src->height;
    dw = width;
    dh = height;
    st = src->type;

    /* for TEST out put first pixel;

       printf("0x%02X%02X%02X\n",
       src->data[0][0].R,
       src->data[0][0].G,
       src->data[0][0].B
       );
    //*/

    if (x < dw && y < dh)
    {
        w = (x + sw < dw) ? sw : (dw - x);
        h = (y + sh < dh) ? sh : (dh - y);
        size = w * sizeof(Pixel);

        if (type == EMPTY || st == SOLID)
        { // src opaque or dest empty
            for (sy = 0; sy < h; sy++)
            {
                sp = src->data[sy];
                dp = &(data[y + sy][x]);
                memcpy(dp, sp, size);
            }
        }
        else
        {
            for (sy = 0; sy < h; sy++)
            {
                for (sx = 0; sx < w; sx++)
                {
                    sp = &(src->data[sy][sx]);
                    dp = &(data[y + sy][x + sx]);
                    if (sp->A == 0x00)
                    { // src pixel transparent
                        //DO Nothing
                    }
                    else if (sp->A == 0xFF || dp->A == 0x00)
                    { // src pixel opaque or dest pixel transparent
                        *dp = *sp;
                    }
                    else
                    {
                        dp->Merge(sp);
                    }
                }
            }
        }
        DetectTransparent();
    }
} // }}}

void PixelArray::Fill(Pixel *color)
{ // {{{
    size_t i, size;
    uint8_t a;
    bool same;
    Pixel *row, *p;

    if (data != NULL)
    {
        a = color->A;
        if (a == 0x00 && type == EMPTY)
            return;

        same = (color->R == a && color->G == a && color->B == a);
        row = data[0];
        if (same)
        {
            size = width * sizeof(Pixel);
            memset(row, a, size);
        }
        else
        {
            for (i = 0, p = row; i < width; i++, p++)
            {
                *p = *color;
            }
        }

        size = width * sizeof(Pixel);
        for (i = 1; i < height; i++)
        {
            memcpy(data[i], row, size);
        }

        type = ((a == 0xFF) ? SOLID : ((a == 0x00) ? EMPTY : ALPHA));
    }
} // }}}

ImageState PixelArray::SetWidth(size_t w)
{ // {{{
    size_t size, *index, *p, x, y;
    double scale;
    Pixel *src, *dst;
    PixelArray newArray, *pixels;

    if (data != NULL)
    {
        if (w > Image::maxWidth)
        {
            SET_ERROR("Beyond the width limit.");
            return FAIL;
        }

        if (w == width)
        {
            return SUCCESS;
        }

        size = w * sizeof(size_t);
        if ((index = (size_t *)malloc(size)) == NULL)
        {
            SET_ERROR("Out of memory.");
            return FAIL;
        }

        scale = ((double)width) / w;
        for (x = 0, p = index; x < w; x++, p++)
        {
            *p = (size_t)(scale * x);
        }

        pixels = &newArray;
        if (pixels->Malloc(w, height) != SUCCESS)
        {
            free(index);
            return FAIL;
        }
        pixels->type = type;

        for (y = 0; y < height; y++)
        {
            src = data[y];
            dst = pixels->data[y];
            for (x = 0, p = index; x < w; x++, p++)
            {
                dst[x] = src[*p];
            }
        }
        free(index);
        Free();
        *this = *pixels;
    }
    return SUCCESS;
} // }}}

ImageState PixelArray::SetHeight(size_t h)
{ // {{{
    PixelArray newArray, *pixels;
    size_t size, y;
    double scale;
    Pixel *src, *dst;

    if (data != NULL)
    {

        if (h > Image::maxHeight)
        {
            return SET_ERROR("Beyond the height limit.");
        }

        if (h == height)
        {
            return SUCCESS;
        }

        pixels = &newArray;
        if (pixels->Malloc(width, h) != SUCCESS)
        {
            return FAIL;
        }
        pixels->type = type;

        size = width * sizeof(Pixel);
        scale = ((double)height) / h;
        for (y = 0; y < h; y++)
        {
            src = data[(size_t)(scale * y)];
            dst = pixels->data[y];
            memcpy(dst, src, size);
        }

        Free();
        *this = *pixels;
    }
    return SUCCESS;
} // }}}

ImageState PixelArray::Resize(size_t w, size_t h, const char *filter)
{
    PixelArray newArray, *pixels;

    if ((int)w < 1)
    {
        w = width * h / height;
    }

    if ((int)h < 1)
    {
        h = height * w / width;
    }

    if (data != NULL)
    {
        if (w > Image::maxWidth)
        {
            return SET_ERROR("Beyond the width limit.");
        }

        if (h > Image::maxHeight)
        {
            return SET_ERROR("Beyond the height limit.");
        }

        if (w == width && h == height)
        {
            return SUCCESS;
        }

        pixels = &newArray;
        if (pixels->Malloc(w, h) != SUCCESS)
        {
            return FAIL;
        }
        pixels->type = type;

        resize(this, pixels, filter);

        Free();
        *this = *pixels;

        // printf( "%d, %d, %d\n", this->data[122][267].R, this->data[122][267].G, this->data[122][267].B);
    }
    return SUCCESS;
}

ImageState PixelArray::Rotate(size_t deg)
{
    PixelArray newArray, *pixels;
    size_t w, h;

    deg = deg % 360;

    if (data != NULL)
    {
        if (deg == 0)
        {
            return SUCCESS;
        }

        pixels = &newArray;
        pixels->type = type;

        if (rotate(this, pixels, deg) != SUCCESS)
        {
            return FAIL;
        }

        Free();
        *this = *pixels;
    }
    return SUCCESS;
}

void PixelArray::DetectTransparent()
{ // {{{
    size_t x, y;
    Pixel *pixel;
    bool empty, opaque, alpha;
    type = EMPTY;

    empty = opaque = alpha = false;

    for (y = 0; y < height; y++)
    {
        pixel = data[y];
        for (x = 0; x < width; x++, pixel++)
        {
            switch (pixel->A)
            {
            case 0x00:
                empty = true;
                break;
            case 0xFF:
                opaque = true;
                break;
            default:
                alpha = true;
                break;
            }

            if (alpha || (empty && opaque))
            {
                type = ALPHA;
                return;
            }
        }
    }
    type = opaque ? SOLID : EMPTY;
} // }}}

extern "C"
{
    napi_value Init(napi_env env, napi_value exports)
    { // {{{
        return Image::Init(env, exports);
    } // }}}
}

NODE_MODULE(NODE_GYP_MODULE_NAME, Init);

// vim600: sw=4 ts=4 fdm=marker syn=cpp
