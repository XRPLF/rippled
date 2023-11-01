//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

// TODO: at some point these might graduate, but for the moment they can live
// here
namespace LBD {
// Why isn't this anywhere ? sizeof(LedgerHeader) is used in places
// but that's just a rough guesstimate good for reserving bytes that
// may not be used in the end.
constexpr std::size_t LedgerHeaderSize = 118;

// TODO: ApplicationImp::loadLedgerFromFile
class STFileReader
{
private:
    std::unique_ptr<std::ifstream> inFile;
    std::unique_ptr<char[]> readBuffer = nullptr;
    beast::Journal _j;

public:
    STFileReader(std::unique_ptr<std::ifstream> inFile, beast::Journal journal)
        : inFile(std::move(inFile)), _j(journal)
    {
    }

    static std::optional<STFileReader>
    open(const std::string& filename, beast::Journal journal)
    {
        auto inFile = std::make_unique<std::ifstream>(
            filename, std::ios::in | std::ios::binary);
        if (!inFile->is_open())
        {
            return std::nullopt;
        }
        return STFileReader(std::move(inFile), journal);
    }

    void
    buffer(size_t buffer_size)
    {
        // TODO: probably pointless, let os do its thing?
        // leger dumps load much slower than with ripple-lib-java
        // maybe just because of the SHAMap structure with tonnes of child
        // hashes vs pointers?
        readBuffer = std::make_unique<char[]>(buffer_size);
        inFile->rdbuf()->pubsetbuf(readBuffer.get(), buffer_size);
    }

    void
    exceptions(std::ios_base::iostate exceptionMask)
    {
        inFile->exceptions(exceptionMask);
    }

    std::vector<uint8_t>
    read(std::size_t size)
    {
        std::vector<uint8_t> data(size);
        inFile->read(reinterpret_cast<char*>(data.data()), size);
        return data;  // I can haz NRVO plz ??
    }

    // RVO ;) surely ? RIP Buffer?
    Blob
    readVL()
    {
        auto bytes = read(1);
        int first = static_cast<int>(bytes[0]);
        JLOG(_j.trace()) << "Read first of lenLen bytes: " << bytes.size()
                         << ", =" << first;
        // Need to read 0 to 2 more bytes, for a max total of 3
        auto lenLen = Serializer::decodeLengthLength(first);
        JLOG(_j.trace()) << "lenLen " << lenLen;
        if (lenLen > 1)
        {
            std::ranges::copy(read(lenLen - 1), std::back_inserter(bytes));
            JLOG(_j.trace()) << "Bytes is now len of " << bytes.size();
        }
        // It's a pity SerialIter can't just wrap a file as a source, no?
        SerialIter data(makeSlice(bytes));
        int size = data.getVLDataLength();
        JLOG(_j.trace()) << "Reading vl Blob with length of " << size;
        return read(size);
    }

    auto
    mapItem()
    {
        return make_shamapitem(readHash(), makeSlice(readVL()));
    }

    // TODO: these aren't used, so?
    std::shared_ptr<STTx const>
    readTx()
    {
        return std::make_shared<STTx const>(SerialIter(makeSlice(readVL())));
    };

    std::shared_ptr<SLE const>
    readLe()
    {
        const uint256& index = readHash();
        return std::make_shared<SLE const>(
            SerialIter(makeSlice(readVL())), index);
    };

    std::uint32_t
    readU32()
    {
        // Keep this all in line else the 4 bytes temp will go out of scope
        return SerialIter(makeSlice(read(4))).get32();
    }

    std::uint64_t
    readU64()
    {
        // Keep this all in line else the 8 bytes temp will go out of scope
        return SerialIter(makeSlice(read(8))).get64();
    }

    LedgerHeader
    readLedgerHeader()
    {
        LedgerHeader header =
            deserializeHeader(makeSlice(read(LedgerHeaderSize)), false);
        header.hash = calculateLedgerHash(header);
        return header;
    }

    // Wow, what is this, TypeScript ?? Welcome to the party!
    template <typename T>
        requires requires(T a) {
            {
                a.data()
            } -> std::convertible_to<void*>;
            {
                a.size()
            } -> std::same_as<std::size_t>;
            requires sizeof(decltype(*(a.data()))) == 1;
        }
    T
    read()
    {
        T object;
        inFile->read(reinterpret_cast<char*>(object.data()), object.size());
        return object;
    }

    uint256
    readHash()
    {
        return read<uint256>();
    }

    std::streampos
    tell()
    {
        return inFile->tellg();
    }

    // TODO: on my MBP M2 Max this takes ~50 seconds while
    //  Java impl only takes 30.
    //  Why is that??? Java has simply
    // public ShaMapNode[] branches = new ShaMapNode[16];
    // TODO: look into multiple thread impl, partitioning the
    // maps No need to worry about "dirtying"/CoW until the map
    // is fully built.
    void
    readItemsIntoSHAMap(
        ripple::SHAMap& into,
        SHAMapNodeType type,
        bool log = false)
    {
        auto items = readU32();  // 640k ought to be enough ...
        JLOG(_j.trace()) << " reading " << items << "items into shamap";
        for (int i = 0; i < items; ++i)
        {
            const auto& item = mapItem();
            if (log && (i < 100 || (i % 100'000 == 0)))
            {
                JLOG(_j.trace())
                    << "read item " << i << " with key " << item->key();
            }
            into.addItem(type, item);
        }
    };
};

class STFileWriter
{
private:
    std::unique_ptr<std::ofstream> outFile;
    STFileWriter(std::unique_ptr<std::ofstream> outFile)
        : outFile(std::move(outFile))
    {
    }

public:
    static std::optional<STFileWriter>
    open(const std::string& filename)
    {
        auto outFile = std::make_unique<std::ofstream>(
            filename, std::ios::out | std::ios::binary);
        if (!outFile->is_open())
        {
            return std::nullopt;
        }
        return STFileWriter(std::move(outFile));
    }

    void
    close()
    {
        outFile->flush();
        outFile->close();
    }

    std::ios::iostate
    state()
    {
        return outFile->rdstate();
    };

    void
    exceptions(std::ios_base::iostate exceptionMask)
    {
        outFile->exceptions(exceptionMask);
    }

    template <typename T>
    void
    write(const T& object)
    {
        outFile->write(
            reinterpret_cast<const char*>(object.data()), object.size());
    }

    void
    write(const SLE& sle)
    {
        write(sle.key());
        writeVl(sle);
    }
    void
    write(const STTx& tx, const STObject& meta)
    {
        write(tx.getTransactionID());
        writeVl(tx);
        writeVl(meta);
    }

    void
    writeVl(const STObject& object)
    {
        writeVl(makeSlice(serializeBlob(object)));
    }

    void
    write(const LedgerHeader& info)
    {
        Serializer ser(LedgerHeaderSize);
        addRaw(info, ser, false);
        write(ser.slice());
    };

    void
    write(uint32_t value)
    {
        Serializer ser(4);
        ser.add32(value);
        write(ser.slice());
    }
    void
    write(uint64_t value)
    {
        // Why can't we have nice things ?
        //   template <typename T>
        //   void write(T value) {
        //       Serializer ser(sizeof(T));
        //       ser.add(value);
        //       write(ser.slice());
        //   }
        // Oh well!
        Serializer ser(8);
        ser.add64(value);
        write(ser.slice());
    }

    void
    write(boost::intrusive_ptr<SHAMapItem const> const& item)
    {
        write(item->key());
        writeVl(item->slice());
    }

    void
    writeVl(const Slice& slice)
    {
        Serializer ser(slice.size() + 3);
        ser.addVL(slice);
        write(ser.slice());
    }

    std::streampos
    tell()
    {
        return outFile->tellp();
    }

    template <typename T>
    auto
    bookmark(T placeholder)
    {
        const std::streampos mark = outFile->tellp();
        write(placeholder);
        const std::streampos target = outFile->tellp();
        return [this, target, mark](T realValue) {
            const std::streampos originalPosition = outFile->tellp();
            outFile->seekp(mark);
            write(realValue);
            if (outFile->tellp() != target)
            {
                // TODO: always throws ? seem inconsistent with not just
                // setting fail/bad bits via outFile->exceptions method
                throw std::runtime_error(
                    "wrong number of bytes written to bookmarked position");
            }
            outFile->seekp(originalPosition);
        };
    }

    // The bookmark method is utilized to handle a scenario where the exact
    // value to be written to the file is not known until a later point. This
    // method is particularly useful in the case of writing the count of SHAMap
    // items to the file, where the count is only known after iterating through
    // the SHAMap.
    //
    // Initially, a placeholder value is written to the file at the current
    // position (mark), which is remembered. Then, the file pointer is advanced,
    // and other data is written to the file.
    //
    // Once the actual value is known, this method allows us to go back (seek)
    // to the marked position, overwrite the placeholder with the actual value,
    // and then restore the file pointer to its original position.
    //
    // This way, we avoid re-iterating the SHAMap just to get the count before
    // starting the actual writing process, thereby enhancing performance,
    // especially when dealing with a large SHAMap.
    //
    // ^ Well thanks Mr Chat, but how many ms is it really going to save us?
    auto
    write(const SHAMap& map)
    {
        // TODO: 640k ought to be enough ;) currently only 1.4M ledger entries
        // so should be good for a while. Json can't even serialize uint64_t
        // without converting it to a string first!
        std::uint32_t n = 0;
        auto bookmarked = bookmark(n);
        map.visitLeaves([this, &n](auto& item) {
            write(item);
            n++;
        });
        bookmarked(n);
        return n;
    }
};
};  // namespace LBD

// TODO: this would be handy, the function that I copy/pasted this
// from was lamentably private.
void
fillLedgerHeader(const LedgerHeader& info, Json::Value& json)
{
    json[jss::parent_hash] = to_string(info.parentHash);
    json[jss::ledger_index] = to_string(info.seq);
    json[jss::ledger_hash] = to_string(info.hash);
    json[jss::transaction_hash] = to_string(info.txHash);
    json[jss::account_hash] = to_string(info.accountHash);
    json[jss::total_coins] = to_string(info.drops);
    json[jss::close_flags] = info.closeFlags;
    json[jss::parent_close_time] =
        info.parentCloseTime.time_since_epoch().count();
    json[jss::close_time] = info.closeTime.time_since_epoch().count();
    json[jss::close_time_resolution] = info.closeTimeResolution.count();
    if (info.closeTime != NetClock::time_point{})
    {
        json[jss::close_time_human] = to_string(info.closeTime);
        if (!getCloseAgree(info))
            json[jss::close_time_estimated] = true;
    }
};

Json::Value
doLedgerBinaryDump(RPC::JsonContext& context)
{
    auto j = context.app.logs().journal("LedgerBinaryDump");

    if (!context.params.isMember(jss::file_name))
    {
        return RPC::make_param_error(std::string(jss::file_name));
    }
    auto fn = context.params[jss::file_name].asString();

    Json::Value jvResult = Json::objectValue;
    JLOG(j.debug()) << "params " << context.params;

    // verify not specified or false if present
    bool dumpMode = !context.params.isMember("verify") ||
        !context.params["verify"].asBool();

    if (dumpMode)
    {
        auto res = RPC::getLedgerByContext(context);
        if (std::holds_alternative<Json::Value>(res))
            return std::get<Json::Value>(res);

        auto lpLedger = std::get<std::shared_ptr<Ledger const>>(res);

        JLOG(j.info()) << "using file for dump: " << fn;
        auto out = LBD::STFileWriter::open(fn);
        if (!out)
        {
            auto err = "can not open file: " + fn;
            JLOG(j.error()) << err;
            return RPC::make_error(rpcUNKNOWN, err);
        }
        out->exceptions(std::ios::failbit | std::ios::badbit);
        JLOG(j.debug()) << "writing ledger header with "
                        << LBD::LedgerHeaderSize << " bytes";
        out->write(lpLedger->info());
        // std::uint64_t won't fit in json, so we have no recourse but to
        // stringify when are bigint literals coming to JSON?
        JLOG(j.info()) << "writing tx map";
        jvResult["total_tx"] = out->write(lpLedger->txMap());
        JLOG(j.info()) << "writing state map";
        jvResult["total_entries"] = out->write(lpLedger->stateMap());
        jvResult["total_bytes"] = std::uint32_t(out->tell());
        out->close();
    }
    else
    {  // Should separate this verify function ;)
        auto in = LBD::STFileReader::open(
            fn, context.app.logs().journal("STFileReader"));
        if (!in)
        {
            auto err = "can not open file: " + fn;
            JLOG(j.error()) << err;
            return RPC::make_error(rpcUNKNOWN, err);
        }
        else
        {
            JLOG(j.debug()) << "trying to read header";
            try
            {
                auto header = in->readLedgerHeader();
                JLOG(j.debug()) << "reader is at position " << in->tell();
                Json::Value json = Json::objectValue;
                fillLedgerHeader(header, json);
                jvResult["ledger"] = json;
                JLOG(j.info()) << "ledger header read" << json;
                {
                    auto txMap = SHAMap(
                        SHAMapType::TRANSACTION, context.app.getNodeFamily());
                    txMap.setUnbacked();
                    in->readItemsIntoSHAMap(
                        txMap, SHAMapNodeType::tnTRANSACTION_MD);
                    jvResult["verified_tx_hash"] =
                        header.txHash == txMap.getHash().as_uint256();
                }
                {
                    auto asMap =
                        SHAMap(SHAMapType::STATE, context.app.getNodeFamily());
                    asMap.setUnbacked();
                    in->readItemsIntoSHAMap(
                        asMap, SHAMapNodeType::tnACCOUNT_STATE);
                    jvResult["verified_account_hash"] =
                        header.accountHash == asMap.getHash().as_uint256();
                }
            }
            catch (const std::exception& e)
            {
                JLOG(j.error())
                    << "there was an error building the maps " << e.what();
                throw e;
            }
        }
    }

    return jvResult;
}
}  // namespace ripple
