void OpenDevFile()
{
	std::string devicefile = "";
	std::string devicepath = "/dev/apci";
	for (const auto &devfile : std::filesystem::directory_iterator(devicepath))
	{
		apci = open(devfile.path().c_str(), O_RDONLY);
		if (apci >= 0)
		{
			devicefile = devfile.path().c_str();
			break;
		}
	}
	Log("Opening device @ " + devicefile);
}
