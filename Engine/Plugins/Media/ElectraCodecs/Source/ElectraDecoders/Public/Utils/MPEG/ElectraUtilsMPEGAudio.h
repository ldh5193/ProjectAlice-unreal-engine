// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include <Containers/Array.h>

namespace ElectraDecodersUtil
{
	namespace MPEG
	{

		class ELECTRADECODERS_API FAACDecoderConfigurationRecord
		{
		public:
			FAACDecoderConfigurationRecord();

			bool ParseFrom(const void* Data, int64 Size);
			void Reset();
			const TArray<uint8>& GetCodecSpecificData() const;

			void SetRawData(const TArray<uint8>& InRawData)
			{
				RawData = InRawData;
			}
			const TArray<uint8>& GetRawData() const
			{
				return RawData;
			}

			int32		SBRSignal;
			int32		PSSignal;
			uint32		ChannelConfiguration;
			uint32		SamplingFrequencyIndex;
			uint32		SamplingRate;
			uint32		ExtSamplingFrequencyIndex;
			uint32		ExtSamplingFrequency;
			uint32		AOT;
			uint32		ExtAOT;
		private:
			TArray<uint8>	CodecSpecificData;
			TArray<uint8>	RawData;
		};


		namespace AACUtils
		{
			int32 ELECTRADECODERS_API GetNumberOfChannelsFromChannelConfiguration(uint32 InChannelConfiguration);
		}


	} // namespace MPEG

} // namespace ElectraDecodersUtil
