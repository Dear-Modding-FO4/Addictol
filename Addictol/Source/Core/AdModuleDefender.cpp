#include <Core/AdModuleDefender.h>
#include <Core/AdUtils.h>

#define AD_DEBUG_MODULEDEFENDER 0

Addictol::ModuleDefender::~ModuleDefender() noexcept
{
	Release();
}

bool Addictol::ModuleDefender::Initialize() noexcept
{
	const auto mod = REX::FModule::GetExecutingModule();

	m_code.section = mod.GetSection(".text");
	m_rData.section = mod.GetSection(".rdata");
	m_rwData.section = mod.GetSection(".data");

#if AD_DEBUG_MODULEDEFENDER
	if (m_code.section.GetSize() > 0)
		REX::INFO("ModuleDefender: .text section found"sv);

	if (m_rData.section.GetSize() > 0)
		REX::INFO("ModuleDefender: .rdata section found"sv);

	if (m_rwData.section.GetSize() > 0)
		REX::INFO("ModuleDefender: .data section found"sv);
#endif // AD_DEBUG_MODULEDEFENDER

	m_code.data = std::make_unique<uint8_t[]>(m_code.section.GetSize());
	m_rData.data = std::make_unique<uint8_t[]>(m_rData.section.GetSize());
	m_rwData.data = std::make_unique<uint8_t[]>(m_rwData.section.GetSize());
	
	if (!m_code.data || !m_rData.data || !m_rwData.data)
	{
		Release();

#if AD_DEBUG_MODULEDEFENDER
		REX::ERROR("ModuleDefender: memory allocate failed"sv);
#endif // AD_DEBUG_MODULEDEFENDER
		return false;
	}

	return true;
}

void Addictol::ModuleDefender::Release() noexcept
{
	m_code.data.reset();
	m_rData.data.reset();
	m_rwData.data.reset();
}

bool Addictol::ModuleDefender::TakeSnapshot() noexcept
{
	if (!m_code.data || !m_rData.data || !m_rwData.data)
		return false;

	memcpy(m_code.data.get(), m_code.section.GetPointer(), m_code.section.GetSize());
	memcpy(m_rData.data.get(), m_rData.section.GetPointer(), m_rData.section.GetSize());
	memcpy(m_rwData.data.get(), m_rwData.section.GetPointer(), m_rwData.section.GetSize());

#if AD_DEBUG_MODULEDEFENDER
	REX::INFO("ModuleDefender: snapshot process..."sv);
#endif // AD_DEBUG_MODULEDEFENDER

	return m_take = true;
}

bool Addictol::ModuleDefender::RestoreFromSnapshot() noexcept
{
	if (!m_take)
		return false;

	RELEX::ScopeLock scope_code(m_code.section.GetPointer(), m_code.section.GetSize());
	RELEX::ScopeLock scope_rData(m_rData.section.GetPointer(), m_rData.section.GetSize());
	RELEX::ScopeLock scope_rwData(m_rwData.section.GetPointer(), m_rwData.section.GetSize());

	memcpy(m_code.section.GetPointer(), m_code.data.get(), m_code.section.GetSize());
	memcpy(m_rData.section.GetPointer(), m_rData.data.get(), m_rData.section.GetSize());
	memcpy(m_rwData.section.GetPointer(), m_rwData.data.get(), m_rwData.section.GetSize());

#if AD_DEBUG_MODULEDEFENDER
	REX::INFO("ModuleDefender: restore process..."sv);
#endif // AD_DEBUG_MODULEDEFENDER

	return std::exchange(m_take, false);
}