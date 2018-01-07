/**
 * Copyright (c) 2011-2017 libbitcoin developers (see AUTHORS)
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
#ifndef LIBBITCOIN_DATABASE_RECORD_HASH_TABLE_IPP
#define LIBBITCOIN_DATABASE_RECORD_HASH_TABLE_IPP

#include <bitcoin/bitcoin.hpp>
#include <bitcoin/database/memory/memory.hpp>
#include <bitcoin/database/primitives/hash_table_header.hpp>
#include <bitcoin/database/primitives/record_row.hpp>

namespace libbitcoin {
namespace database {

template <typename KeyType, typename IndexType, typename LinkType>
record_hash_table<KeyType, IndexType, LinkType>::record_hash_table(
    storage& file, IndexType buckets, size_t value_size)
  : header_(file, buckets),
    manager_(file, hash_table_header<IndexType, LinkType>::size(buckets),
        value_size)
{
}

template <typename KeyType, typename IndexType, typename LinkType>
bool record_hash_table<KeyType, IndexType, LinkType>::create()
{
    return header_.create() && manager_.create();
}

template <typename KeyType, typename IndexType, typename LinkType>
bool record_hash_table<KeyType, IndexType, LinkType>::start()
{
    return header_.start() && manager_.start();
}

template <typename KeyType, typename IndexType, typename LinkType>
void record_hash_table<KeyType, IndexType, LinkType>::sync()
{
    return manager_.sync();
}

// This is not limited to storing unique key values. If duplicate keyed values
// are store then retrieval and unlinking will fail as these multiples cannot
// be differentiated except in the order written.
template <typename KeyType, typename IndexType, typename LinkType>
LinkType record_hash_table<KeyType, IndexType, LinkType>::store(
    const KeyType& key, write_function write)
{
    // Allocate and populate new unlinked record.
    row record(manager_);
    const auto index = record.create(key, write);

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    create_mutex_.lock();

    // Link new record.next to current first record.
    record.link(read_bucket_value(key));

    // Link header to new record as the new first.
    link(key, index);

    create_mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    // Return the array index of the new record (starts at key, not value).
    return index;
}

// Execute a writer against a key's buffer if the key is found.
// Return the array index of the found value (or not_found).
template <typename KeyType, typename IndexType, typename LinkType>
LinkType record_hash_table<KeyType, IndexType, LinkType>::update(
    const KeyType& key, write_function write)
{
    // Find start item...
    auto current = read_bucket_value(key);

    // TODO: implement hash_table_iterable/hash_table_iterator.
    // Iterate through list...
    while (current != not_found)
    {
        row item(manager_, current);

        // Found, update data and return index.
        if (item.compare(key))
        {
            const auto memory = item.data();
            auto serial = make_unsafe_serializer(memory->buffer());
            write(serial);
            return current;
        }

        // Critical Section
        ///////////////////////////////////////////////////////////////////////
        shared_lock lock(update_mutex_);
        current = item.next_index();
        ///////////////////////////////////////////////////////////////////////
    }

    return not_found;
}

// This is limited to returning the first of multiple matching key values.
template <typename KeyType, typename IndexType, typename LinkType>
LinkType record_hash_table<KeyType, IndexType, LinkType>::offset(
    const KeyType& key) const
{
    // Find start item...
    auto current = read_bucket_value(key);

    // TODO: implement record_table_iterable/record_table_iterator.
    // Iterate through list...
    while (current != header_.empty)
    {
        const_row item(manager_, current);

        // Found, return index.
        if (item.compare(key))
            return item.offset();

        const auto previous = current;
        current = item.next_index();
        BITCOIN_ASSERT(previous != current);
    }

    return not_found;
}

// This is limited to returning the first of multiple matching key values.
template <typename KeyType, typename IndexType, typename LinkType>
memory_ptr record_hash_table<KeyType, IndexType, LinkType>::find(
    const KeyType& key) const
{
    // Find start item...
    auto current = read_bucket_value(key);

    // TODO: implement hash_table_iterable/hash_table_iterator.
    // Iterate through list...
    while (current != not_found)
    {
        const_row item(manager_, current);

        // Found, return pointer.
        if (item.compare(key))
            return item.data();

        // Critical Section
        ///////////////////////////////////////////////////////////////////////
        shared_lock lock(update_mutex_);
        current = item.next_index();
        ///////////////////////////////////////////////////////////////////////
    }

    return nullptr;
}

template <typename KeyType, typename IndexType, typename LinkType>
memory_ptr record_hash_table<KeyType, IndexType, LinkType>::get(
    LinkType record) const
{
    return manager_.get(record);
}

// Unlink is not safe for concurrent write.
// This is limited to unlinking the first of multiple matching key values.
template <typename KeyType, typename IndexType, typename LinkType>
bool record_hash_table<KeyType, IndexType, LinkType>::unlink(
    const KeyType& key)
{
    // Find start item...
    auto previous = read_bucket_value(key);
    row begin_item(manager_, previous);

    // If start item has the key then unlink from buckets.
    if (begin_item.compare(key))
    {
        //*********************************************************************
        const auto next = begin_item.next_index();
        //*********************************************************************

        link(key, next);
        return true;
    }

    ///////////////////////////////////////////////////////////////////////////
    update_mutex_.lock_shared();
    auto current = begin_item.next_index();
    update_mutex_.unlock_shared();
    ///////////////////////////////////////////////////////////////////////////

    // TODO: implement hash_table_iterable/hash_table_iterator.
    // Iterate through list...
    while (current != not_found)
    {
        row item(manager_, current);

        // Found, unlink current item from previous.
        if (item.compare(key))
        {
            row previous_item(manager_, previous);

            // Critical Section
            ///////////////////////////////////////////////////////////////////
            update_mutex_.lock_upgrade();
            const auto next = item.next_index();
            update_mutex_.unlock_upgrade_and_lock();
            //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
            previous_item.write_next_index(next);
            update_mutex_.unlock();
            ///////////////////////////////////////////////////////////////////
            return true;
        }

        previous = current;

        // Critical Section
        ///////////////////////////////////////////////////////////////////////
        shared_lock lock(update_mutex_);
        current = item.next_index();
        ///////////////////////////////////////////////////////////////////////
    }

    return false;
}

// private
template <typename KeyType, typename IndexType, typename LinkType>
IndexType record_hash_table<KeyType, IndexType, LinkType>::bucket_index(
    const KeyType& key) const
{
    return hash_table_header<IndexType, LinkType>::remainder(key,
        header_.buckets());
}

// private
template <typename KeyType, typename IndexType, typename LinkType>
LinkType record_hash_table<KeyType, IndexType, LinkType>::read_bucket_value(
    const KeyType& key) const
{
    return header_.read(bucket_index(key));
}

// private
template <typename KeyType, typename IndexType, typename LinkType>
void record_hash_table<KeyType, IndexType, LinkType>::link(const KeyType& key,
    LinkType begin)
{
    header_.write(bucket_index(key), begin);
}

} // namespace database
} // namespace libbitcoin

#endif
