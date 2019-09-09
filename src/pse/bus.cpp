#include "bus.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/String.h"
#include "dma.h"
#include <cstdio>
Log_SetChannel(Bus);

Bus::Bus() = default;

Bus::~Bus() = default;

bool Bus::Initialize(System* system, DMA* dma, GPU* gpu)
{
  if (!LoadBIOS())
    return false;

  m_dma = dma;
  m_gpu = gpu;
  return true;
}

void Bus::Reset()
{
  m_ram.fill(static_cast<u8>(0));
}

bool Bus::DoState(StateWrapper& sw)
{
  return false;
}

bool Bus::ReadByte(PhysicalMemoryAddress cpu_address, PhysicalMemoryAddress bus_address, u8* value)
{
  u32 temp = 0;
  const bool result = DispatchAccess<MemoryAccessType::Read, MemoryAccessSize::Byte>(cpu_address, bus_address, temp);
  *value = Truncate8(temp);
  return result;
}

bool Bus::ReadWord(PhysicalMemoryAddress cpu_address, PhysicalMemoryAddress bus_address, u16* value)
{
  u32 temp = 0;
  const bool result =
    DispatchAccess<MemoryAccessType::Read, MemoryAccessSize::HalfWord>(cpu_address, bus_address, temp);
  *value = Truncate16(temp);
  return result;
}

bool Bus::ReadDWord(PhysicalMemoryAddress cpu_address, PhysicalMemoryAddress bus_address, u32* value)
{
  return DispatchAccess<MemoryAccessType::Read, MemoryAccessSize::Word>(cpu_address, bus_address, *value);
}

bool Bus::WriteByte(PhysicalMemoryAddress cpu_address, PhysicalMemoryAddress bus_address, u8 value)
{
  u32 temp = ZeroExtend32(value);
  return DispatchAccess<MemoryAccessType::Read, MemoryAccessSize::Byte>(cpu_address, bus_address, temp);
}

bool Bus::WriteWord(PhysicalMemoryAddress cpu_address, PhysicalMemoryAddress bus_address, u16 value)
{
  u32 temp = ZeroExtend32(value);
  return DispatchAccess<MemoryAccessType::Read, MemoryAccessSize::HalfWord>(cpu_address, bus_address, temp);
}

bool Bus::WriteDWord(PhysicalMemoryAddress cpu_address, PhysicalMemoryAddress bus_address, u32 value)
{
  return DispatchAccess<MemoryAccessType::Write, MemoryAccessSize::Word>(cpu_address, bus_address, value);
}

bool Bus::LoadBIOS()
{
  std::FILE* fp = std::fopen("SCPH1001.BIN", "rb");
  if (!fp)
    return false;

  std::fseek(fp, 0, SEEK_END);
  const u32 size = static_cast<u32>(std::ftell(fp));
  std::fseek(fp, 0, SEEK_SET);

  if (size != m_bios.size())
  {
    Log_ErrorPrintf("BIOS image mismatch, expecting %u bytes, got %u bytes", static_cast<u32>(m_bios.size()), size);
    std::fclose(fp);
    return false;
  }

  if (std::fread(m_bios.data(), 1, m_bios.size(), fp) != m_bios.size())
  {
    Log_ErrorPrintf("Failed to read BIOS image");
    std::fclose(fp);
    return false;
  }

  std::fclose(fp);

#if 1
  auto Patch = [this](u32 address, u32 value) { std::memcpy(&m_bios[address], &value, sizeof(value)); };
  Patch(0x6F0C, 0x24010001); // addiu $at, $zero, 1
  Patch(0x6F14, 0xaf81a9c0); // sw at, -0x5640(gp)
#endif

  return true;
}

bool Bus::DoInvalidAccess(MemoryAccessType type, MemoryAccessSize size, PhysicalMemoryAddress cpu_address,
                          PhysicalMemoryAddress bus_address, u32& value)
{
  SmallString str;
  str.AppendString("Invalid bus ");
  if (size == MemoryAccessSize::Byte)
    str.AppendString("byte");
  if (size == MemoryAccessSize::HalfWord)
    str.AppendString("word");
  if (size == MemoryAccessSize::Word)
    str.AppendString("dword");
  str.AppendCharacter(' ');
  if (type == MemoryAccessType::Read)
    str.AppendString("read");
  else
    str.AppendString("write");

  str.AppendFormattedString(" at address 0x%08X (virtual address 0x%08X)", bus_address, cpu_address);
  if (type == MemoryAccessType::Write)
    str.AppendFormattedString(" (value 0x%08X)", value);

  Log_ErrorPrint(str);
  if (type == MemoryAccessType::Read)
    value = UINT32_C(0xFFFFFFFF);

  return true;
}

bool Bus::ReadExpansionRegion2(MemoryAccessSize size, u32 offset, u32& value)
{
  offset &= EXP2_MASK;

  // rx/tx buffer empty
  if (offset == 0x21)
  {
    value = 0x04 | 0x08;
    return true;
  }

  return DoInvalidAccess(MemoryAccessType::Read, size, EXP2_BASE | offset, EXP2_BASE | offset, value);
}

bool Bus::WriteExpansionRegion2(MemoryAccessSize size, u32 offset, u32 value)
{
  offset &= EXP2_MASK;

  if (offset == 0x23)
  {
    if (value == '\r')
      return true;

    if (value == '\n')
    {
      if (!m_tty_line_buffer.IsEmpty())
        Log_InfoPrintf("TTY: %s", m_tty_line_buffer.GetCharArray());
      m_tty_line_buffer.Clear();
    }
    else
    {
      m_tty_line_buffer.AppendCharacter(Truncate8(value));
    }

    return true;
  }

  if (offset == 0x41)
  {
    Log_WarningPrintf("BIOS POST status: %02X", value & UINT32_C(0x0F));
    return true;
  }

  return DoInvalidAccess(MemoryAccessType::Write, size, EXP2_BASE | offset, EXP2_BASE | offset, value);
}

bool Bus::ReadSPU(MemoryAccessSize size, u32 offset, u32& value)
{
  if (offset == 0x1AE)
  {
    value = 0;
    return true;
  }

  //return DoInvalidAccess(MemoryAccessType::Write, size, SPU_BASE | offset, SPU_BASE | offset, value);
  value = 0;
  return true;
}

bool Bus::WriteSPU(MemoryAccessSize size, u32 offset, u32 value)
{
  // Transfer FIFO
  if (offset == 0x1A8)
    return true;

  // SPUCNT
  if (offset == 0x1AA)
    return true;

  //return DoInvalidAccess(MemoryAccessType::Write, size, SPU_BASE | offset, SPU_BASE | offset, value);
  return true;
}

bool Bus::DoReadDMA(MemoryAccessSize size, u32 offset, u32& value)
{
  Assert(size == MemoryAccessSize::Word);
  value = m_dma->ReadRegister(offset);
  return true;
}

bool Bus::DoWriteDMA(MemoryAccessSize size, u32 offset, u32& value)
{
  Assert(size == MemoryAccessSize::Word);
  m_dma->WriteRegister(offset, value);
  return true;
}
