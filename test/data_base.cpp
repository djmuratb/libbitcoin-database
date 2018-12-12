/**
* Copyright (c) 2011-2018 libbitcoin developers (see AUTHORS)
*
* This file is part of libbitcoin.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU Affero General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Affero General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <boost/test/unit_test.hpp>

#include <future>
#include <memory>
#include <boost/filesystem.hpp>
#include <bitcoin/database.hpp>
#include "utility/utility.hpp"

using namespace bc;
using namespace bc::chain;
using namespace bc::database;
using namespace bc::wallet;
using namespace boost::system;
using namespace boost::filesystem;

static void
test_block_exists(const data_base& interface, size_t height,
                       const block& block, bool index_addresses, bool candidate)
{
   const auto& address_store = interface.addresses();
   const auto block_hash = block.hash();
   auto result = interface.blocks().get(height, candidate);
   auto result_by_hash = interface.blocks().get(block_hash);

   BOOST_REQUIRE(result);
   BOOST_REQUIRE(result_by_hash);
   BOOST_REQUIRE(result.hash() == block_hash);
   BOOST_REQUIRE(result_by_hash.hash() == block_hash);
   BOOST_REQUIRE_EQUAL(result.height(), height);
   BOOST_REQUIRE_EQUAL(result_by_hash.height(), height);
   BOOST_REQUIRE_EQUAL(result.transaction_count(), block.transactions().size());
   BOOST_REQUIRE_EQUAL(result_by_hash.transaction_count(), block.transactions().size());

   // TODO: test tx offsets (vs. tx hashes).

   for (size_t i = 0; i < block.transactions().size(); ++i)
   {
       const auto& tx = block.transactions()[i];
       const auto tx_hash = tx.hash();
       ////BOOST_REQUIRE(result.transaction_hash(i) == tx_hash);
       ////BOOST_REQUIRE(result_by_hash.transaction_hash(i) == tx_hash);

       auto result_tx = interface.transactions().get(tx_hash);
       BOOST_REQUIRE(result_tx);
       BOOST_REQUIRE(result_by_hash);
       BOOST_REQUIRE(result_tx.transaction().hash() == tx_hash);
       BOOST_REQUIRE_EQUAL(result_tx.height(), height);
       BOOST_REQUIRE_EQUAL(result_tx.position(), i);

       if (!tx.is_coinbase())
       {
           for (auto j = 0u; j < tx.inputs().size(); ++j)
           {
               const auto& input = tx.inputs()[j];
               input_point spend{ tx_hash, j };
               BOOST_REQUIRE_EQUAL(spend.index(), j);

               if (!index_addresses)
                   continue;

               const auto addresses = input.addresses();
               const auto& prevout = input.previous_output();
               ////const auto address = prevout.metadata.cache.addresses();

               for (const payment_address& address: addresses)
               {
                   auto history = address_store.get(address.hash());
                   auto found = false;

                   for (const payment_record& row: history)
                   {
                       if (row.hash() == tx_hash && row.index() == j)
                       {
                           BOOST_REQUIRE_EQUAL(row.height(), height);
                           found = true;
                           break;
                       }
                   }

                   BOOST_REQUIRE(found);
               }
           }
       }

       if (!index_addresses)
           return;

       for (size_t j = 0; j < tx.outputs().size(); ++j)
       {
           const auto& output = tx.outputs()[j];
           output_point outpoint{ tx_hash, static_cast<uint32_t>(j) };
           const auto addresses = output.addresses();

           for (const payment_address& address: addresses)
           {
               auto history = address_store.get(address.hash());
               auto found = false;
               
               for (const payment_record& row: history)
               {
                   BOOST_REQUIRE(row.is_valid());

                   if (row.hash() == tx_hash && row.index() == j)
                   {
                       BOOST_REQUIRE_EQUAL(row.height(), height);
                       BOOST_REQUIRE_EQUAL(row.data(), output.value());
                       found = true;
                       break;
                   }
               }

               BOOST_REQUIRE(found);
           }
       }
   }
}

// static void
// test_block_not_exists(const data_base& interface, const block& block0,
//    bool index_addresses)
// {
//    const auto& address_store = interface.history();

//    // Popped blocks still exist in the block hash table, but not confirmed.
//    const auto block_hash = block0.hash();
//    const auto result = interface.blocks().get(block_hash);
//    BOOST_REQUIRE(!is_confirmed(result.state()));

//    for (size_t i = 0; i < block0.transactions().size(); ++i)
//    {
//        const auto& tx = block0.transactions()[i];
//        const auto tx_hash = tx.hash();

//        if (!tx.is_coinbase())
//        {
//            for (size_t j = 0; j < tx.inputs().size(); ++j)
//            {
//                const auto& input = tx.inputs()[j];
//                input_point spend{ tx_hash, static_cast<uint32_t>(j) };
//                auto r0_spend = interface.spends().get(input.previous_output());
//                BOOST_REQUIRE(!r0_spend.is_valid());

//                if (!index_addresses)
//                    continue;

//                const auto addresses = input.addresses();
//                ////const auto& prevout = input.previous_output();
//                ////const auto address = prevout.metadata.cache.addresses();

//                for (const auto& address: addresses)
//                {
//                    auto history = address_store.get(address.hash(), 0, 0);
//                    auto found = false;

//                    for (const auto& row: history)
//                    {
//                        if (row.point() == spend)
//                        {
//                            found = true;
//                            break;
//                        }
//                    }

//                    BOOST_REQUIRE(!found);
//                }
//            }
//        }

//        if (!index_addresses)
//            return;

//        for (size_t j = 0; j < tx.outputs().size(); ++j)
//        {
//            const auto& output = tx.outputs()[j];
//            output_point outpoint{ tx_hash, static_cast<uint32_t>(j) };
//            const auto addresses = output.addresses();

//            for (const auto& address: addresses)
//            {
//                auto history = address_store.get(address.hash(), 0, 0);
//                auto found = false;

//                for (const auto& row: history)
//                {
//                    if (row.point() == outpoint)
//                    {
//                        found = true;
//                        break;
//                    }
//                }

//                BOOST_REQUIRE(!found);
//            }
//        }
//    }
// }

static chain_state::data
data_for_chain_state()
{
    chain_state::data value;
    value.height = 1;
    value.bits = { 0, { 0 } };
    value.version = { 1, { 0 } };
    value.timestamp = { 0, 0, { 0 } };
    return value;
}

static void
set_state(block& block) {
    auto state = std::make_shared<chain_state>(
        chain_state{ data_for_chain_state(), {}, 0, 0, bc::settings() });
    block.header().metadata.state = state;
}

static block
read_block(const std::string hex)
{
   data_chunk data;
   BOOST_REQUIRE(decode_base16(data, hex));
   block result;
   BOOST_REQUIRE(result.from_data(data));
   set_state(result);
   return result;
}

static void
store_block_transactions(data_base& instance, const block& block, size_t forks) {
   for (const auto& tx: block.transactions()) {
       instance.store(tx, forks);
   }
}

#define DIRECTORY "data_base"

struct data_base_setup_fixture
{
    data_base_setup_fixture()
    {
        test::clear_path(DIRECTORY);
    }
    
    ~data_base_setup_fixture()
    {
        test::clear_path(DIRECTORY);
    }
};


BOOST_FIXTURE_TEST_SUITE(data_base_tests, data_base_setup_fixture)

#define TRANSACTION1 "0100000001537c9d05b5f7d67b09e5108e3bd5e466909cc9403ddd98bc42973f366fe729410600000000ffffffff0163000000000000001976a914fe06e7b4c88a719e92373de489c08244aee4520b88ac00000000"

#define MAINNET_BLOCK1 \
"010000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000982" \
"051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e61bc6649ffff00" \
"1d01e3629901010000000100000000000000000000000000000000000000000000000000000" \
"00000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e8" \
"53519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a60" \
"4f8141781e62294721166bf621e73a82cbf2342c858eeac00000000"

#define MAINNET_BLOCK2 \
"010000004860eb18bf1b1620e37e9490fc8a427514416fd75159ab86688e9a8300000000d5f" \
"dcc541e25de1c7a5addedf24858b8bb665c9f36ef744ee42c316022c90f9bb0bc6649ffff00" \
"1d08d2bd6101010000000100000000000000000000000000000000000000000000000000000" \
"00000000000ffffffff0704ffff001d010bffffffff0100f2052a010000004341047211a824" \
"f55b505228e4c3d5194c1fcfaa15a456abdf37f9b9d97a4040afc073dee6c89064984f03385" \
"237d92167c13e236446b417ab79a0fcae412ae3316b77ac00000000"

#define MAINNET_BLOCK3 \
"01000000bddd99ccfda39da1b108ce1a5d70038d0a967bacb68b6b63065f626a0000000044f" \
"672226090d85db9a9f2fbfe5f0f9609b387af7be5b7fbb7a1767c831c9e995dbe6649ffff00" \
"1d05e0ed6d01010000000100000000000000000000000000000000000000000000000000000" \
"00000000000ffffffff0704ffff001d010effffffff0100f2052a0100000043410494b9d3e7" \
"6c5b1629ecf97fff95d7a4bbdac87cc26099ada28066c6ff1eb9191223cd897194a08d0c272" \
"6c5747f1db49e8cf90e75dc3e3550ae9b30086f3cd5aaac00000000"

class data_base_accessor
 : public data_base
{
public:
    data_base_accessor(const bc::database::settings& settings)
        : data_base(settings)
    {
    }

    bool push_all(block_const_ptr_list_const_ptr blocks, const config::checkpoint& fork_point)
    {
        return data_base::push_all(blocks, fork_point);
    }

    bool push_all(header_const_ptr_list_const_ptr headers, const config::checkpoint& fork_point)
    {
        return data_base::push_all(headers, fork_point);
    }
    
    code push_header(const header& header, size_t height, uint32_t mtp)
    {
        return data_base::push_header(header, height, mtp);
    }

    code push_block(const block& block, size_t height)
    {
        return data_base::push_block(block, height);
    }
    
    code store(const transaction& tx, uint32_t forks)
    {
        return data_base::store(tx, forks);
    }
    
};

static void
test_heights(const data_base& instance, size_t candidate_height_in, size_t confirmed_height_in)
{    
    size_t candidate_height = 0u;
    size_t confirmed_height = 0u;
    BOOST_REQUIRE(instance.blocks().top(candidate_height, true));
    BOOST_REQUIRE(instance.blocks().top(confirmed_height, false));
    
    BOOST_REQUIRE_EQUAL(candidate_height, candidate_height_in);
    BOOST_REQUIRE_EQUAL(confirmed_height, confirmed_height_in);   
}

// static code pop_above_result(data_base_accessor& instance,
//    block_const_ptr_list_ptr out_blocks, const config::checkpoint& fork_point,
//    dispatcher& dispatch)
// {
//    std::promise<code> promise;
//    const auto handler = [&promise](code ec)
//    {
//        promise.set_value(ec);
//    };
//    instance.pop_above(out_blocks, fork_point, dispatch, handler);
//    return promise.get_future().get();
// }

// static code push_all_result(data_base_accessor& instance,
//    block_const_ptr_list_const_ptr in_blocks, size_t index, size_t height,
//    dispatcher& dispatch)
// {
//    std::promise<code> promise;
//    const auto handler = [&promise](code ec)
//    {
//        promise.set_value(ec);
//    };
//    instance.push_next(in_blocks, index, height, dispatch, handler);
//    return promise.get_future().get();
// }

BOOST_AUTO_TEST_CASE(data_base__create__block_transactions_index_interaction__success)
{
   create_directory(DIRECTORY);
   database::settings settings;
   settings.directory = DIRECTORY;
   settings.index_addresses = false;
   settings.flush_writes = false;
   settings.file_growth_rate = 42;
   settings.block_table_buckets = 42;
   settings.transaction_table_buckets = 42;
   settings.address_table_buckets = 42;
  
   data_base instance(settings); 
   
   static const auto bc_settings = bc::settings(bc::config::settings::mainnet);
   const chain::block genesis = bc_settings.genesis_block;
   BOOST_REQUIRE(instance.create(genesis));
   
   test_heights(instance, 0u, 0u);

   transaction tx1;
   data_chunk wire_tx1;
   BOOST_REQUIRE(decode_base16(wire_tx1, TRANSACTION1));
   BOOST_REQUIRE(tx1.from_data(wire_tx1));

   const auto hash1 = tx1.hash();
   BOOST_REQUIRE(!instance.transactions().get(hash1));   
}

BOOST_AUTO_TEST_CASE(data_base__create__genesis_block_available__success)
{
   create_directory(DIRECTORY);
   database::settings settings;
   settings.directory = DIRECTORY;
   settings.index_addresses = false;
   settings.flush_writes = false;
   settings.file_growth_rate = 42;
   settings.block_table_buckets = 42;
   settings.transaction_table_buckets = 42;
   settings.address_table_buckets = 42;
  
   data_base instance(settings); 
   
   static const auto bc_settings = bc::settings(bc::config::settings::mainnet);
   const chain::block genesis = bc_settings.genesis_block;
   BOOST_REQUIRE(instance.create(genesis));

   test_block_exists(instance, 0, genesis, settings.index_addresses, false);
}

BOOST_AUTO_TEST_CASE(data_base__push__adds_to_blocks_and_transactions_validates_and_confirms__success)
{
   create_directory(DIRECTORY);
   database::settings settings;
   settings.directory = DIRECTORY;
   settings.index_addresses = false;
   settings.flush_writes = false;
   settings.file_growth_rate = 42;
   settings.block_table_buckets = 42;
   settings.transaction_table_buckets = 42;
   settings.address_table_buckets = 42;
  
   data_base instance(settings); 
   
   static const auto bc_settings = bc::settings(bc::config::settings::mainnet);
   const chain::block genesis = bc_settings.genesis_block;
   BOOST_REQUIRE(instance.create(genesis));

   const auto block1 = read_block(MAINNET_BLOCK1);   

   // setup ends
   
   BOOST_REQUIRE_EQUAL(instance.push(block1, 1), error::success);

   // test conditions
   
   test_block_exists(instance, 1, block1, settings.index_addresses, false);
   test_heights(instance, 1u, 1u);
}

// BLOCK ORGANIZER tests
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(data_base__push_block__not_existing___fails)
{
    create_directory(DIRECTORY);
    database::settings settings;
    settings.directory = DIRECTORY;
    settings.index_addresses = false;
    settings.flush_writes = false;
    settings.file_growth_rate = 42;
    settings.block_table_buckets = 42;
    settings.transaction_table_buckets = 42;
    settings.address_table_buckets = 42;
    
    data_base_accessor instance(settings); 
    
    static const auto bc_settings = bc::settings(bc::config::settings::mainnet);
    const chain::block genesis = bc_settings.genesis_block;
    BOOST_REQUIRE(instance.create(genesis));
    
    const auto block1 = read_block(MAINNET_BLOCK1);
    store_block_transactions(instance, block1, 1);
    
    // setup ends
    
    BOOST_REQUIRE_EQUAL(instance.push_block(block1, 1), error::operation_failed);

    // test conditions

    test_heights(instance, 0u, 0u);
}

BOOST_AUTO_TEST_CASE(data_base__push_block__incorrect_height___fails)
{
   create_directory(DIRECTORY);
   database::settings settings;
   settings.directory = DIRECTORY;
   settings.index_addresses = false;
   settings.flush_writes = false;
   settings.file_growth_rate = 42;
   settings.block_table_buckets = 42;
   settings.transaction_table_buckets = 42;
   settings.address_table_buckets = 42;

   data_base_accessor instance(settings);

   static const auto bc_settings = bc::settings(bc::config::settings::mainnet);
   const chain::block genesis = bc_settings.genesis_block;
   BOOST_REQUIRE(instance.create(genesis));

   const auto block1 = read_block(MAINNET_BLOCK1);
   store_block_transactions(instance, block1, 1);

   BOOST_REQUIRE_EQUAL(instance.push_header(block1.header(), 1, 100), error::success);
   BOOST_REQUIRE_EQUAL(instance.candidate(block1), error::success);
   test_heights(instance, 1u, 0u);

   // setup ends

   BOOST_REQUIRE_EQUAL(instance.push_block(block1, 2), error::store_block_invalid_height);
}

BOOST_AUTO_TEST_CASE(data_base__push_block__missing_parent___fails)
{
   create_directory(DIRECTORY);
   database::settings settings;
   settings.directory = DIRECTORY;
   settings.index_addresses = false;
   settings.flush_writes = false;
   settings.file_growth_rate = 42;
   settings.block_table_buckets = 42;
   settings.transaction_table_buckets = 42;
   settings.address_table_buckets = 42;

   data_base_accessor instance(settings);

   static const auto bc_settings = bc::settings(bc::config::settings::mainnet);
   const chain::block genesis = bc_settings.genesis_block;
   BOOST_REQUIRE(instance.create(genesis));

   auto block1 = read_block(MAINNET_BLOCK1);
   store_block_transactions(instance, block1, 1);
   block1.set_header(chain::header{});

   // setup ends

   BOOST_REQUIRE_EQUAL(instance.push_header(block1.header(), 1, 100), error::store_block_missing_parent);
}

BOOST_AUTO_TEST_CASE(data_base__push_block_and_update__already_candidated___success)
{
   create_directory(DIRECTORY);
   database::settings settings;
   settings.directory = DIRECTORY;
   settings.index_addresses = false;
   settings.flush_writes = false;
   settings.file_growth_rate = 42;
   settings.block_table_buckets = 42;
   settings.transaction_table_buckets = 42;
   settings.address_table_buckets = 42;
  
   data_base_accessor instance(settings); 
   
   static const auto bc_settings = bc::settings(bc::config::settings::mainnet);
   const chain::block genesis = bc_settings.genesis_block;
   BOOST_REQUIRE(instance.create(genesis));
   
   const auto block1 = read_block(MAINNET_BLOCK1);
   store_block_transactions(instance, block1, 1);

   BOOST_REQUIRE_EQUAL(instance.push_header(block1.header(), 1, 100), error::success);
   BOOST_REQUIRE_EQUAL(instance.candidate(block1), error::success);
   test_heights(instance, 1u, 0u);

   // setup ends
   
   BOOST_REQUIRE_EQUAL(instance.push_block(block1, 1), error::success);
   BOOST_REQUIRE_EQUAL(instance.update(block1, 1), error::success);

   // test conditions

   test_heights(instance, 1u, 1u);
   test_block_exists(instance, 1, block1, settings.index_addresses, false);
}

BOOST_AUTO_TEST_CASE(data_base__push_all_and_update__already_candidated___success)
{
   create_directory(DIRECTORY);
   database::settings settings;
   settings.directory = DIRECTORY;
   settings.index_addresses = false;
   settings.flush_writes = false;
   settings.file_growth_rate = 42;
   settings.block_table_buckets = 42;
   settings.transaction_table_buckets = 42;
   settings.address_table_buckets = 42;
  
   data_base_accessor instance(settings); 
   
   static const auto bc_settings = bc::settings(bc::config::settings::mainnet);
   const chain::block genesis = bc_settings.genesis_block;
   BOOST_REQUIRE(instance.create(genesis));
   
   const auto block1 = read_block(MAINNET_BLOCK1);

   block_const_ptr block1_ptr = std::make_shared<const message::block>(read_block(MAINNET_BLOCK1));
   block_const_ptr block2_ptr = std::make_shared<const message::block>(read_block(MAINNET_BLOCK2));
   block_const_ptr block3_ptr = std::make_shared<const message::block>(read_block(MAINNET_BLOCK3));
   const auto blocks_push_ptr = std::make_shared<const block_const_ptr_list>(block_const_ptr_list{
           block1_ptr, block2_ptr, block3_ptr });
   store_block_transactions(instance, *block1_ptr, 1);
   store_block_transactions(instance, *block2_ptr, 1);
   store_block_transactions(instance, *block3_ptr, 1);

   const auto headers_push_ptr = std::make_shared<const header_const_ptr_list>(header_const_ptr_list{
           std::make_shared<const message::header>(block1_ptr->header()),
               std::make_shared<const message::header>(block2_ptr->header()),
               std::make_shared<const message::header>(block3_ptr->header())
               });
      
   BOOST_REQUIRE(instance.push_all(headers_push_ptr, config::checkpoint(genesis.hash(), 0)));
   for(const auto block_ptr: *blocks_push_ptr) {
       BOOST_REQUIRE_EQUAL(instance.candidate(*block_ptr), error::success);
   }

   test_heights(instance, 3u, 0u);

   // setup ends
   
   BOOST_REQUIRE(instance.push_all(blocks_push_ptr, config::checkpoint(genesis.hash(), 0)));
   BOOST_REQUIRE_EQUAL(instance.update(*block1_ptr, 1), error::success);
   BOOST_REQUIRE_EQUAL(instance.update(*block2_ptr, 2), error::success);
   BOOST_REQUIRE_EQUAL(instance.update(*block3_ptr, 3), error::success);

   // test conditions

   test_heights(instance, 3u, 3u);
   test_block_exists(instance, 1, *block1_ptr, settings.index_addresses, false);
   test_block_exists(instance, 2, *block2_ptr, settings.index_addresses, false);
   test_block_exists(instance, 3, *block3_ptr, settings.index_addresses, false);
}

// BOOST_AUTO_TEST_CASE(data_base__pushpop__test)
// {
//    std::cout << "begin data_base push/pop test" << std::endl;

//    create_directory(DIRECTORY);
//    database::settings settings;
//    settings.directory = DIRECTORY;
//    settings.index_addresses = true;
//    settings.flush_writes = false;
//    settings.file_growth_rate = 42;
//    settings.block_table_buckets = 42;
//    settings.transaction_table_buckets = 42;
//    settings.address_table_buckets = 42;

//    size_t height;
//    threadpool pool(1);
//    dispatcher dispatch(pool, "test");
//    data_base_accessor instance(settings);
//    const auto block0 = block::genesis_mainnet();
//    BOOST_REQUIRE(instance.create(block0));
//    test_block_exists(instance, 0, block0, settings.index_addresses);
//    BOOST_REQUIRE(instance.blocks().top(height, false));
//    BOOST_REQUIRE_EQUAL(height, 0);

//    // This tests a missing parent, not a database failure.
//    // A database failure would prevent subsequent read/write operations.
//    std::cout << "push block #1 (store_block_missing_parent)" << std::endl;
//    auto invalid_block1 = read_block(MAINNET_BLOCK1);
//    invalid_block1.set_header(chain::header{});
//    BOOST_REQUIRE_EQUAL(instance.push(invalid_block1, 1), error::store_block_missing_parent);

//    std::cout << "push block #1" << std::endl;
//    const auto block1 = read_block(MAINNET_BLOCK1);
//    test_block_not_exists(instance, block1, settings.index_addresses);
//    BOOST_REQUIRE_EQUAL(instance.push(block1, 1), error::success);
//    test_block_exists(instance, 0, block0, settings.index_addresses);
//    BOOST_REQUIRE(instance.blocks().top(height, false));
//    BOOST_REQUIRE_EQUAL(height, 1u);
//    test_block_exists(instance, 1, block1, settings.index_addresses);

//    std::cout << "push_all blocks #2 & #3" << std::endl;
//    const auto block2_ptr = std::make_shared<const message::block>(read_block(MAINNET_BLOCK2));
//    const auto block3_ptr = std::make_shared<const message::block>(read_block(MAINNET_BLOCK3));
//    const auto blocks_push_ptr = std::make_shared<const block_const_ptr_list>(block_const_ptr_list{ block2_ptr, block3_ptr });
//    test_block_not_exists(instance, *block2_ptr, settings.index_addresses);
//    test_block_not_exists(instance, *block3_ptr, settings.index_addresses);
//    BOOST_REQUIRE_EQUAL(push_all_result(instance, blocks_push_ptr, 0, 2, dispatch), error::success);
//    test_block_exists(instance, 1, block1, settings.index_addresses);
//    BOOST_REQUIRE(instance.blocks().top(height, false));
//    BOOST_REQUIRE_EQUAL(height, 3u);
//    test_block_exists(instance, 3, *block3_ptr, settings.index_addresses);
//    test_block_exists(instance, 2, *block2_ptr, settings.index_addresses);

//    std::cout << "insert block #2 (store_block_invalid_height)" << std::endl;
//    BOOST_REQUIRE_EQUAL(instance.push(*block2_ptr, 2), error::store_block_invalid_height);

//    std::cout << "pop_above block 1 (blocks #2 & #3)" << std::endl;
//    const auto blocks_popped_ptr = std::make_shared<block_const_ptr_list>();
//    BOOST_REQUIRE_EQUAL(pop_above_result(instance, blocks_popped_ptr, { block1.hash(), 1 }, dispatch), error::success);
//    BOOST_REQUIRE(instance.blocks().top(height, false));
//    BOOST_REQUIRE_EQUAL(height, 1u);
//    BOOST_REQUIRE_EQUAL(blocks_popped_ptr->size(), 2u);
//    BOOST_REQUIRE(*(*blocks_popped_ptr)[0] == *block2_ptr);
//    BOOST_REQUIRE(*(*blocks_popped_ptr)[1] == *block3_ptr);
//    test_block_not_exists(instance, *block3_ptr, settings.index_addresses);
//    test_block_not_exists(instance, *block2_ptr, settings.index_addresses);
//    test_block_exists(instance, 1, block1, settings.index_addresses);
//    test_block_exists(instance, 0, block0, settings.index_addresses);

//    std::cout << "push block #3 (store_block_invalid_height)" << std::endl;
//    BOOST_REQUIRE_EQUAL(instance.push(*block3_ptr, 3), error::store_block_invalid_height);

//    std::cout << "insert block #2" << std::endl;
//    BOOST_REQUIRE_EQUAL(instance.push(*block2_ptr, 2), error::success);
//    BOOST_REQUIRE(instance.blocks().top(height, false));
//    BOOST_REQUIRE_EQUAL(height, 2u);

//    std::cout << "pop_above block 0 (block #1 & #2)" << std::endl;
//    blocks_popped_ptr->clear();
//    BOOST_REQUIRE_EQUAL(pop_above_result(instance, blocks_popped_ptr, { block0.hash(), 0 }, dispatch), error::success);
//    BOOST_REQUIRE(instance.blocks().top(height, false));
//    BOOST_REQUIRE_EQUAL(height, 0u);
//    BOOST_REQUIRE(*(*blocks_popped_ptr)[0] == block1);
//    BOOST_REQUIRE(*(*blocks_popped_ptr)[1] == *block2_ptr);
//    test_block_not_exists(instance, block1, settings.index_addresses);
//    test_block_not_exists(instance, *block2_ptr, settings.index_addresses);
//    test_block_exists(instance, 0, block0, settings.index_addresses);

//    std::cout << "end push/pop test" << std::endl;
// }

BOOST_AUTO_TEST_SUITE_END()
