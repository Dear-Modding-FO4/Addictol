#include <Core/Settings/AdSettingPersistence.h>

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: AdSettingsGenerator <Output File>";
		return 2;
	}

	std::string output, error;
	if (!Addictol::BuildSettingsDocumentToml("", output, error))
	{
		std::cerr << "Could not generate the Config File: " << error << '\n';
		return 1;
	}

	if (!Addictol::WriteAtomically(std::filesystem::path{ argv[1] }, output, error))
	{
		std::cerr << "Could not write the Config File: " << argv[1] << ": " << error << '\n';
		return 1;
	}

	std::cout << "Generated the Config File: " << argv[1] << '\n';
	return 0;
}
