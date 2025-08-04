hf::Group createNeXusGroup(hf::File& current, const std::string& name, const std::string& nxclass);
hf::Group createNeXusGroup(hf::Group& current, const std::string& name, const std::string& nxclass);
void createNeXusStructure(const std::string&filename, hf::File& out_file);
