

#include <scorum/plugins/raw_block/raw_block_api.hpp>
#include <scorum/plugins/raw_block/raw_block_plugin.hpp>

#include <string>

namespace scorum {
namespace plugin {
namespace raw_block {

raw_block_plugin::raw_block_plugin(application* app)
    : plugin(app)
{
}
raw_block_plugin::~raw_block_plugin()
{
}

std::string raw_block_plugin::plugin_name() const
{
    return "raw_block";
}

void raw_block_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
    print_greeting();
}

void raw_block_plugin::plugin_startup()
{
    app().register_api_factory<raw_block_api>("raw_block_api");
}

void raw_block_plugin::plugin_shutdown()
{
}
}
}
} // scorum::plugin::raw_block

SCORUM_DEFINE_PLUGIN(raw_block, scorum::plugin::raw_block::raw_block_plugin)
