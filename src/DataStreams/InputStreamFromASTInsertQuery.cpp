#include <DataStreams/InputStreamFromASTInsertQuery.h>

#include <DataStreams/AddingDefaultsBlockInputStream.h>
#include <DataStreams/BlockIO.h>
#include <IO/ConcatReadBuffer.h>
#include <IO/ReadBufferFromMemory.h>
#include <Interpreters/Context.h>
#include <Interpreters/InterpreterSetQuery.h>
#include <Parsers/ASTInsertQuery.h>
#include <Storages/ColumnsDescription.h>
#include <Storages/IStorage.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
    extern const int INVALID_USAGE_OF_INPUT;
}


InputStreamFromASTInsertQuery::InputStreamFromASTInsertQuery(
    const ASTPtr & ast,
    const Block & header,
    ContextPtr context,
    const ASTPtr & input_function)
{
    const auto * ast_insert_query = ast->as<ASTInsertQuery>();

    if (!ast_insert_query)
        throw Exception("Logical error: query requires data to insert, but it is not INSERT query", ErrorCodes::LOGICAL_ERROR);

    String format = ast_insert_query->format;
    if (format.empty())
    {
        if (input_function)
            throw Exception("FORMAT must be specified for function input()", ErrorCodes::INVALID_USAGE_OF_INPUT);
        format = "Values";
    }

    res_stream = context->getInputFormat(format, input_buffer, header, context->getSettings().max_insert_block_size);

    if (context->getSettingsRef().input_format_defaults_for_omitted_fields && ast_insert_query->table_id && !input_function)
    {
        StoragePtr storage = DatabaseCatalog::instance().getTable(ast_insert_query->table_id, context);
        auto metadata_snapshot = storage->getInMemoryMetadataPtr();
        const auto & columns = metadata_snapshot->getColumns();
        if (columns.hasDefaults())
            res_stream = std::make_shared<AddingDefaultsBlockInputStream>(res_stream, columns, context);
    }
}

InputStreamFromASTInsertQuery::InputStreamFromASTInsertQuery(
    const ASTPtr & ast,
    ReadBuffer & tail,
    const Block & header,
    ContextPtr context,
    const ASTPtr & input_function) : InputStreamFromASTInsertQuery(ast, header, context, input_function)
{
    /// Data could be in parsed (ast_insert_query.data) and in not parsed yet (input_buffer_tail_part) part of query.
    const auto * ast_insert_query = ast->as<ASTInsertQuery>();

    auto ast_buffer = std::make_unique<ReadBufferFromMemory>(
        ast_insert_query->data, ast_insert_query->data ? ast_insert_query->end - ast_insert_query->data : 0);

    if (ast_insert_query->data)
        input_buffer.appendBuffer(std::move(ast_buffer));

    input_buffer.appendBuffer(wrapReadBufferReference(tail));

    /// NOTE: Must not read from 'tail' before read all between 'ast_insert_query.data' and 'ast_insert_query.end'.
    ///       because 'query.data' could refer to memory piece, used as buffer for 'tail'.
}

}
