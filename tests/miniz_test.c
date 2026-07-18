#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

static int test_deflate(void)
{
    static const unsigned char source[] =
        "Morpheus static compression round trip: Morpheus static compression round trip";
    mz_ulong compressed_size = mz_compressBound((mz_ulong)sizeof(source));
    mz_ulong restored_size = (mz_ulong)sizeof(source);
    unsigned char *compressed = (unsigned char *)malloc((size_t)compressed_size);
    unsigned char restored[sizeof(source)];
    int result;

    if (!compressed) return 0;
    result = mz_compress2(
        compressed,
        &compressed_size,
        source,
        (mz_ulong)sizeof(source),
        MZ_BEST_COMPRESSION);
    if (result == MZ_OK) {
        result = mz_uncompress(
            restored,
            &restored_size,
            compressed,
            compressed_size);
    }
    free(compressed);
    return result == MZ_OK && restored_size == sizeof(source) &&
        memcmp(restored, source, sizeof(source)) == 0;
}

static int test_zip(void)
{
    static const char name[] = "assets/config.json";
    static const char content[] = "{\"schema_version\":1,\"status\":\"ready\"}";
    mz_zip_archive writer;
    mz_zip_archive reader;
    mz_zip_archive_file_stat statistics;
    void *archive_data = NULL;
    size_t archive_size = 0;
    void *extracted = NULL;
    size_t extracted_size = 0;
    int file_index;
    int success = 0;

    memset(&writer, 0, sizeof(writer));
    memset(&reader, 0, sizeof(reader));
    if (!mz_zip_writer_init_heap(&writer, 0, 0) ||
        !mz_zip_writer_add_mem(
            &writer,
            name,
            content,
            sizeof(content) - 1,
            MZ_BEST_COMPRESSION) ||
        !mz_zip_writer_finalize_heap_archive(&writer, &archive_data, &archive_size)) {
        mz_zip_writer_end(&writer);
        return 0;
    }
    mz_zip_writer_end(&writer);

    if (!mz_zip_reader_init_mem(&reader, archive_data, archive_size, 0)) goto cleanup;
    file_index = mz_zip_reader_locate_file(&reader, name, NULL, 0);
    if (file_index < 0 ||
        !mz_zip_reader_file_stat(&reader, (mz_uint)file_index, &statistics) ||
        statistics.m_uncomp_size != sizeof(content) - 1) {
        mz_zip_reader_end(&reader);
        goto cleanup;
    }
    extracted = mz_zip_reader_extract_to_heap(
        &reader,
        (mz_uint)file_index,
        &extracted_size,
        0);
    success = extracted && extracted_size == sizeof(content) - 1 &&
        memcmp(extracted, content, extracted_size) == 0;
    mz_zip_reader_end(&reader);

cleanup:
    mz_free(extracted);
    mz_free(archive_data);
    return success;
}

int main(void)
{
    if (!test_deflate()) {
        fprintf(stderr, "Deflate round trip failed\n");
        return 1;
    }
    if (!test_zip()) {
        fprintf(stderr, "In-memory ZIP round trip failed\n");
        return 2;
    }
    puts("PASS: embedded miniz deflate and in-memory ZIP round trips");
    return 0;
}
