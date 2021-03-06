/*
 * Copyright © 2015 Matthew Gregan
 *
 * This program is made available under an ISC-style license.  See the
 * accompanying file LICENSE for details.
 */
#include <assert.h>
#include <string.h>

#include <neaacdec.h>

#include "gmp-audio-decode.h"
#include "gmp-audio-host.h"
#include "gmp-platform.h"

class FAAD2AudioDecoder : public GMPAudioDecoder {
public:
  FAAD2AudioDecoder(GMPAudioHost* audioHost);
  virtual ~FAAD2AudioDecoder();

  // GMPAudioDecoder interface
  virtual void InitDecode(GMPAudioCodec const & codecSettings, GMPAudioDecoderCallback * callback);
  virtual void Decode(GMPAudioSamples * encodedSamples);
  virtual void Reset();
  virtual void Drain();
  virtual void DecodingComplete();

private:
  GMPAudioHost * audio_host_;
  GMPAudioDecoderCallback * callback_;

  NeAACDecHandle decoder_;
};

static GMPPlatformAPI * gAPI;

extern "C" GMPErr
GMPInit(GMPPlatformAPI * api)
{
  assert(!gAPI);
  gAPI = api;
  return GMPNoErr;
}

extern "C" GMPErr
GMPGetAPI(char const * name, void * hostAPI, void ** pluginAPI)
{
  GMPAudioHost* audioHost = static_cast<GMPAudioHost *>(hostAPI);

  if (strcmp(name, GMP_API_AUDIO_DECODER) != 0) {
    return GMPInvalidArgErr;
  }

  *pluginAPI = new FAAD2AudioDecoder(audioHost);
  return GMPNoErr;
}

extern "C" void
GMPShutdown()
{
  delete gAPI;
  gAPI = nullptr;
}

FAAD2AudioDecoder::FAAD2AudioDecoder(GMPAudioHost* audioHost)
  : audio_host_(audioHost)
  , callback_(nullptr)
{
  assert(audioHost);
}

FAAD2AudioDecoder::~FAAD2AudioDecoder()
{
  if (decoder_) {
    NeAACDecClose(decoder_);
  }
}

void
FAAD2AudioDecoder::InitDecode(GMPAudioCodec const & codecSettings, GMPAudioDecoderCallback * callback)
{
  assert(callback);
  callback_ = callback;

  if (codecSettings.mCodecType != kGMPAudioCodecAAC) {
    callback_->Error(GMPInvalidArgErr);
    return;
  }

  decoder_ = NeAACDecOpen();
  if (!decoder_) {
    callback_->Error(GMPAllocErr);
    return;
  }

  NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(decoder_);
  assert(config);
  config->outputFormat = FAAD_FMT_16BIT;
  NeAACDecSetConfiguration(decoder_, config);

  unsigned long rate;
  unsigned char channels;
  long r = NeAACDecInit2(decoder_, const_cast<unsigned char *>(codecSettings.mExtraData), codecSettings.mExtraDataLen,
                         &rate, &channels);
  if (r != 0) {
    callback_->Error(GMPGenericErr);
    NeAACDecClose(decoder_);
    decoder_ = nullptr;
    return;
  }
}

void
FAAD2AudioDecoder::Decode(GMPAudioSamples * encodedSamples)
{
  assert(callback_);
  if (!decoder_) {
    callback_->Error(GMPGenericErr);
    return;
  }

  assert(encodedSamples->GetFormat() == kGMPAudioEncodedSamples);

  NeAACDecFrameInfo frame_info;
  void * samples = NeAACDecDecode(decoder_, &frame_info, encodedSamples->Buffer(), encodedSamples->Size());
  if (frame_info.error != 0) {
    callback_->Error(GMPDecodeErr);
    return;
  }
  assert(frame_info.bytesconsumed == encodedSamples->Size());

  GMPAudioSamples* output;
  GMPErr err = audio_host_->CreateSamples(kGMPAudioIS16Samples, &output);
  if (GMP_FAILED(err)) {
    callback_->Error(GMPDecodeErr);
    return;
  }
  if (frame_info.samples > 0) {
    output->SetBufferSize(frame_info.samples * sizeof(int16_t));
    memcpy(output->Buffer(), samples, output->Size());

    output->SetTimeStamp(encodedSamples->TimeStamp());
    output->SetChannels(frame_info.channels);
    output->SetRate(frame_info.samplerate);
    callback_->Decoded(output);
  }

  encodedSamples->Destroy();
  callback_->InputDataExhausted();
}

void
FAAD2AudioDecoder::Reset()
{
  assert(callback_);
  assert(decoder_);

  NeAACDecPostSeekReset(decoder_, 0);
  callback_->ResetComplete();
}

void
FAAD2AudioDecoder::Drain()
{
  assert(callback_);
  assert(decoder_);

  callback_->DrainComplete();
}

void
FAAD2AudioDecoder::DecodingComplete()
{
  assert(callback_);
  assert(decoder_);

  delete this;
}
