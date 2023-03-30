#pragma once

#include <Columns/ColumnString.h>

#include <string_view>

#include <iostream>

namespace DB
{

namespace extractKV
{

class StateHandler
{
public:
    enum State
    {
        // Skip characters until it finds a valid first key character. Might jump to READING_KEY, READING_QUOTED_KEY or END.
        WAITING_KEY,
        // Tries to read a key. Might jump to WAITING_KEY, WAITING_VALUE or END.
        READING_KEY,
        // Tries to read an quoted/ quoted key. Might jump to WAITING_KEY, READING_KV_DELIMITER or END.
        READING_QUOTED_KEY,
        // Tries to read the key value pair delimiter. Might jump to WAITING_KEY, WAITING_VALUE or END.
        READING_KV_DELIMITER,
        // Skip characters until it finds a valid first value character. Might jump to READING_QUOTED_VALUE or READING_VALUE.
        WAITING_VALUE,
        // Tries to read a value. Jumps to FLUSH_PAIR.
        READING_VALUE,
        // Tries to read an quoted/ quoted value. Might jump to FLUSH_PAIR or END.
        READING_QUOTED_VALUE,
        // In this state, both key and value have already been collected and should be flushed. Might jump to WAITING_KEY or END.
        FLUSH_PAIR,
        END
    };

    struct NextState
    {
        std::size_t position_in_string;
        State state;
    };

    StateHandler() = default;
    StateHandler(const StateHandler &) = default;

    virtual ~StateHandler() = default;
};


class StringWriter
{
    ColumnString & col;
    ColumnString::Chars & chars;
    UInt64 prev_commit_pos = 0;

public:
    StringWriter(ColumnString & col_)
        : col(col_),
          chars(col.getChars())
    {}

    ~StringWriter()
    {
        // Make sure that ColumnString invariants are not broken.
        if (!isEmpty())
            reset();
    }

    void append(std::string_view new_data)
    {
        chars.insert(new_data.begin(), new_data.end());
    }

    template <typename T>
    void append(const T * begin, const T * end)
    {
        chars.insert(begin, end);
    }

    void reset()
    {
        chars.resize_assume_reserved(prev_commit_pos);
    }

    bool isEmpty() const
    {
        return chars.size() == prev_commit_pos;
    }

    void commit()
    {
        if (isEmpty())
            return;

        chars.push_back('\0');
        std::cerr << "Committing on " << &col << " prev_commit_pos: " << prev_commit_pos << " new offset: " << chars.size() << " there will be " << col.getOffsets().size() + 1 << " rows" << std::endl;
        col.getOffsets().emplace_back(chars.size());
        prev_commit_pos = chars.size();
    }

    std::string_view uncommittedChunk() const
    {
        return std::string_view(chars.raw_data() + prev_commit_pos, chars.raw_data() + chars.size());
    }

//    const ColumnString & data() const
//    {
//        return col;
//    }
};


}

// TODO(vnemkov): Debug stuff, remove before merging

template <typename T, bool prepend_length>
struct CustomQuoted
{
    const char * start_quote = "\"";
    const char * end_quote = "\"";

    const T & value;
};

template <bool prepend_with_length, typename T>
CustomQuoted<T, prepend_with_length> customQuote(const char * start_quote, const T & value, const char * end_quote = nullptr)
{
    assert(start_quote != nullptr);

    return CustomQuoted<T, prepend_with_length>{
        .start_quote = start_quote,
        .end_quote = end_quote ? end_quote : start_quote,
        .value = value
    };
}

inline auto fancyQuote(const std::string_view & value)
{
    return CustomQuoted<std::string_view, true>{
        .start_quote = "«",
        .end_quote = "»",
        .value = value
    };
}

template <typename T, bool prepend_length>
std::ostream & operator<<(std::ostream & ostr, const CustomQuoted<T, prepend_length> & val)
{
    if constexpr (prepend_length)
    {
        ostr << "(" << val.value.length() << ") ";
    }

    return ostr << val.start_quote << val.value << val.end_quote;
}

}
