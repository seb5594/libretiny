#include "protocol_codec.h"

#include <app/MessageDef/InvokeRequestMessage.h>

namespace esphome::libretiny_matter_protocol {

CHIP_ERROR encode_invoke(uint16_t endpoint, uint32_t command, chip::MutableByteSpan &output) {
  if (command > 2 || endpoint == 0xffff || output.data() == nullptr)
    return CHIP_ERROR_INVALID_ARGUMENT;
  chip::TLV::TLVWriter writer;
  writer.Init(output);
  chip::app::InvokeRequestMessage::Builder request;
  ReturnErrorOnFailure(request.Init(&writer));
  request.SuppressResponse(false).TimedRequest(false);
  ReturnErrorOnFailure(request.GetError());
  auto &commands = request.CreateInvokeRequests();
  ReturnErrorOnFailure(commands.GetError());
  auto &entry = commands.CreateCommandData();
  ReturnErrorOnFailure(entry.GetError());
  auto &path = entry.CreatePath();
  path.EndpointId(endpoint).ClusterId(6).CommandId(command);
  ReturnErrorOnFailure(path.EndOfCommandPathIB());
  chip::TLV::TLVType outer;
  ReturnErrorOnFailure(writer.StartContainer(chip::TLV::ContextTag(1), chip::TLV::kTLVType_Structure, outer));
  ReturnErrorOnFailure(writer.EndContainer(outer));
  ReturnErrorOnFailure(entry.EndOfCommandDataIB());
  ReturnErrorOnFailure(commands.EndOfInvokeRequests());
  ReturnErrorOnFailure(request.EndOfInvokeRequestMessage());
  ReturnErrorOnFailure(writer.Finalize());
  output.reduce_size(writer.GetLengthWritten());
  return CHIP_NO_ERROR;
}

CHIP_ERROR inspect_invoke(chip::ByteSpan input, InvokeInfo &output) {
  // Limit work for the inspection tool. This is not a server command handler.
  if (input.empty() || input.size() > 512 || input.data() == nullptr)
    return CHIP_ERROR_INVALID_ARGUMENT;
  chip::TLV::TLVReader reader;
  reader.Init(input);
  chip::app::InvokeRequestMessage::Parser request;
  ReturnErrorOnFailure(request.Init(reader));
  bool timed;
  ReturnErrorOnFailure(request.GetTimedRequest(&timed));
  if (timed)
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
  InvokeInfo info{};
  ReturnErrorOnFailure(request.GetInteractionModelRevision(&info.revision));
  chip::app::InvokeRequests::Parser commands;
  ReturnErrorOnFailure(request.GetInvokeRequests(&commands));
  chip::TLV::TLVReader entries;
  commands.GetReader(&entries);
  ReturnErrorOnFailure(entries.Next(chip::TLV::kTLVType_Structure, chip::TLV::AnonymousTag()));
  chip::app::CommandDataIB::Parser entry;
  ReturnErrorOnFailure(entry.Init(entries));
  chip::app::CommandPathIB::Parser path;
  ReturnErrorOnFailure(entry.GetPath(&path));
  ReturnErrorOnFailure(path.GetEndpointId(&info.endpoint));
  ReturnErrorOnFailure(path.GetClusterId(&info.cluster));
  ReturnErrorOnFailure(path.GetCommandId(&info.command));
  chip::TLV::TLVReader fields;
  ReturnErrorOnFailure(entry.GetFields(&fields));
  if (fields.GetType() != chip::TLV::kTLVType_Structure)
    return CHIP_ERROR_WRONG_TLV_TYPE;
  chip::TLV::TLVType outer;
  ReturnErrorOnFailure(fields.EnterContainer(outer));
  if (fields.Next() != CHIP_END_OF_TLV)
    return CHIP_ERROR_INVALID_ARGUMENT;
  ReturnErrorOnFailure(fields.ExitContainer(outer));
  if (entries.Next() != CHIP_END_OF_TLV)
    return CHIP_ERROR_INVALID_ARGUMENT;
  ReturnErrorOnFailure(request.ExitContainer());
  if (info.endpoint == 0xffff || info.cluster != 6 || info.command > 2)
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
  output = info;
  return CHIP_NO_ERROR;
}

}  // namespace esphome::libretiny_matter_protocol
