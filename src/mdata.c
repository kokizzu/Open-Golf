#define _CRT_SECURE_NO_WARNINGS

#include "mdata.h"

#include <assert.h>

#include "base64.h"
#include "data_stream.h"
#include "hotloader.h"
#include "mstring.h"
#include "log.h"

#define _MDATA_VAL_MAX_NAME 32

enum _mdata_file_val_type {
    _MDATA_FILE_VAL_TYPE_INT,
    _MDATA_FILE_VAL_TYPE_UINT64,
    _MDATA_FILE_VAL_TYPE_STRING,
    _MDATA_FILE_VAL_TYPE_BIN_DATA,
};

typedef struct _mdata_file_val {
    enum _mdata_file_val_type type;
    union {
        int int_val;
        uint64_t uint64_val;
        struct {
            char *data;
            int len;
        } data_val;
        char *string_val;
    };
    char name[_MDATA_VAL_MAX_NAME + 1];
    bool user_set;
} _mdata_file_val_t;

typedef vec_t(_mdata_file_val_t) _vec_mdata_file_val_t;

struct mdata_file {
    _vec_mdata_file_val_t vals; 
};

typedef vec_t(mdata_file_t*) _vec_mdata_file_ptr_t;

typedef struct _mdata_extension_handler {
    vec_mfile_t files;
    vec_mfiletime_t file_load_times;
    _vec_mdata_file_ptr_t mdata_files;

    void *udata;
    bool (*mdata_file_creator)(mfile_t file, mdata_file_t *mdata_file, void *udata);
    bool (*mdata_file_handler)(const char *file_path, mdata_file_t *mdata_file, void *udata);
} _mdata_extension_handler_t;

typedef vec_t(_mdata_extension_handler_t) _vec_mdata_extension_handler_t;
static _vec_mdata_extension_handler_t _extension_handlers;

void mdata_init(void) {
    vec_init(&_extension_handlers);
}

mdata_file_t *_load_mdata_file(mfile_t file) {
    if (!mfile_load_data(&file)) {
        return NULL;
    }

    mdata_file_t *mdata_file = malloc(sizeof(mdata_file_t));
    vec_init(&mdata_file->vals);
    int i = 0;
    while (i < file.data_len) {
        int type_len = 0;
        char type[32]; 

        int name_len = 0;
        char name[_MDATA_VAL_MAX_NAME + 1];

        while (file.data[i] != ' ') {
            if (i >= file.data_len) assert(false);
            type[type_len++] = file.data[i++];
            if (type_len >= 32) {
                assert(false);
            }
        }
        type[type_len] = 0;

        if (i >= file.data_len) assert(false);
        i++;

        while (file.data[i] != ' ') {
            if (i >= file.data_len) assert(false);
            name[name_len++] = file.data[i++];
            if (name_len >= _MDATA_VAL_MAX_NAME + 1) {
                assert(false);
            }
        }
        name[name_len] = 0;

        if (i >= file.data_len) assert(false);
        i++;

        if (strcmp(type, "int") == 0) {
            while (file.data[i++] != '\n');
        }
        else if (strcmp(type, "uint64") == 0) {
            while (file.data[i++] != '\n');
            assert(false);
        }
        else if (strcmp(type, "string") == 0) {
            mstring_t string;
            mstring_init(&string, "");
            while (file.data[i] != '\n') {
                mstring_append_char(&string, file.data[i++]);
            }
            i++;
            mdata_file_add_val_string(mdata_file, name, string.cstr, false);
        }
        else if (strcmp(type, "bin_data") == 0) {
            while (file.data[i++] != '\n');
        }
        else {
            assert(false);
        }
    }
    assert(i == file.data_len);

    mfile_free_data(&file);
    return mdata_file;
}

static void _delete_mdata_file(mdata_file_t *mdata_file) {
    free(mdata_file);
} 

void mdata_add_extension_handler(const char *ext, bool (*mdata_file_creator)(mfile_t files, mdata_file_t *mdata_files, void *udata), bool (*mdata_file_handler)(const char *file_path, mdata_file_t *mdata_file, void *udata), void *udata) {
    _mdata_extension_handler_t handler;
    vec_init(&handler.files);
    vec_init(&handler.file_load_times);
    vec_init(&handler.mdata_files);
    handler.udata = udata;
    handler.mdata_file_creator = mdata_file_creator;

    mdir_t dir;
    mdir_init(&dir, "data", true);
    for (int i = 0; i < dir.num_files; i++) {
        mfile_t file = dir.files[i];
        if (strcmp(file.ext, ext) != 0) {
            continue;
        }

        mfile_t data_file = mfile_append_extension(file.path, ".mdata");
        mdata_file_t *mdata_file = _load_mdata_file(data_file);

        mfiletime_t file_load_time;
        mfile_get_time(&file, &file_load_time);

        vec_push(&handler.files, file);
        vec_push(&handler.file_load_times, file_load_time);
        vec_push(&handler.mdata_files, mdata_file);

        m_logf("mdata: Importing %s\n", file.path);
        mdata_file_creator(file, mdata_file, udata);
        mdata_file_handler(file.path, mdata_file, udata);
    }

    for (int i = 0; i < handler.files.length; i++) {
        mdata_file_t *mdata_file = handler.mdata_files.data[i];

        mstring_t str; 
        mstring_init(&str, "");
        for (int j = 0; j < mdata_file->vals.length; j++) {
            _mdata_file_val_t file_val = mdata_file->vals.data[j];
            switch (file_val.type) {
                case _MDATA_FILE_VAL_TYPE_INT:
                    {
                        mstring_appendf(&str, "int %s %d\n", file_val.name, file_val.int_val);
                    }
                    break;
                case _MDATA_FILE_VAL_TYPE_UINT64:
                    {
                        mstring_appendf(&str, "uint64 %s %u\n", file_val.name, file_val.uint64_val);
                    }
                    break;
                case _MDATA_FILE_VAL_TYPE_STRING:
                    {
                        mstring_appendf(&str, "string %s %s\n", file_val.name, file_val.string_val);
                    }
                    break;
                case _MDATA_FILE_VAL_TYPE_BIN_DATA:
                    {
                        char *encoding = base64_encode((const unsigned char*)file_val.data_val.data, file_val.data_val.len);
                        mstring_appendf(&str, "bin_data %s %s\n", file_val.name, encoding);
                        free(encoding);
                    }
                    break;
            }
        }

        mfile_t data_file = mfile_append_extension(handler.files.data[i].path, ".mdata");
        mfile_set_data(&data_file, str.cstr, str.len);
        mstring_deinit(&str);
    }

    vec_push(&_extension_handlers, handler);
}

static _mdata_file_val_t *_get_mdata_file_val(mdata_file_t *mdata_file, const char *field) {
    for (int i = 0; i < mdata_file->vals.length; i++) {
        _mdata_file_val_t *mdata_val = &mdata_file->vals.data[i];
        if (strcmp(mdata_val->name, field) == 0) {
            return mdata_val;
        }
    }
    return NULL;
}

bool mdata_file_get_val_int(mdata_file_t *mdata_file, const char *field, int *val) {
    _mdata_file_val_t *mdata_val = _get_mdata_file_val(mdata_file, field);
    if (mdata_val && mdata_val->type == _MDATA_FILE_VAL_TYPE_INT) {
        *val = mdata_val->int_val;
        return true;
    }
    return false;
}

bool mdata_file_get_val_uint64(mdata_file_t *mdata_file, const char *field, uint64_t *val) {
    _mdata_file_val_t *mdata_val = _get_mdata_file_val(mdata_file, field);
    if (mdata_val && mdata_val->type == _MDATA_FILE_VAL_TYPE_UINT64) {
        *val = mdata_val->uint64_val;
        return true;
    }
    return false;
}

bool mdata_file_get_val_string(mdata_file_t *mdata_file, const char *field, const char **string) {
    _mdata_file_val_t *mdata_val = _get_mdata_file_val(mdata_file, field);
    if (mdata_val && mdata_val->type == _MDATA_FILE_VAL_TYPE_STRING) {
        *string = mdata_val->string_val;
        return true;
    }
    return false;
}

bool mdata_file_get_val_binary_data(mdata_file_t *mdata_file, const char *field, char **data, int *data_len) {
    _mdata_file_val_t *mdata_val = _get_mdata_file_val(mdata_file, field);
    if (mdata_val && mdata_val->type == _MDATA_FILE_VAL_TYPE_BIN_DATA) {
        *data = mdata_val->data_val.data;
        *data_len = mdata_val->data_val.len;
        return true;
    }
    return false;
}

void mdata_file_add_val_int(mdata_file_t *mdata_file, const char *field, int int_val, bool user_set) {
    _mdata_file_val_t *mdata_val = _get_mdata_file_val(mdata_file, field);
    if (mdata_val) {
        if (user_set && mdata_val->type == _MDATA_FILE_VAL_TYPE_INT) {
            return;
        }
    }
    else {
        _mdata_file_val_t val;
        memset(&val, 0, sizeof(val));
        vec_push(&mdata_file->vals, val);
        mdata_val = &vec_last(&mdata_file->vals);
    }

    mdata_val->type = _MDATA_FILE_VAL_TYPE_INT;
    mdata_val->int_val = int_val;
    strncpy(mdata_val->name, field, _MDATA_VAL_MAX_NAME);
    mdata_val->name[_MDATA_VAL_MAX_NAME] = 0;
    mdata_val->user_set = user_set;
}

void mdata_file_add_val_uint64(mdata_file_t *mdata_file, const char *field, uint64_t uint64_val, bool user_set) {
    _mdata_file_val_t *mdata_val = _get_mdata_file_val(mdata_file, field);
    if (mdata_val) {
        if (user_set && mdata_val->type == _MDATA_FILE_VAL_TYPE_UINT64) {
            return;
        }
    }
    else {
        _mdata_file_val_t val;
        memset(&val, 0, sizeof(val));
        vec_push(&mdata_file->vals, val);
        mdata_val = &vec_last(&mdata_file->vals);
    }

    mdata_val->type = _MDATA_FILE_VAL_TYPE_UINT64;
    mdata_val->uint64_val = uint64_val;
    strncpy(mdata_val->name, field, _MDATA_VAL_MAX_NAME);
    mdata_val->name[_MDATA_VAL_MAX_NAME] = 0;
    mdata_val->user_set = user_set;
}

void mdata_file_add_val_string(mdata_file_t *mdata_file, const char *field, const char *string_val, bool user_set) {
    _mdata_file_val_t *mdata_val = _get_mdata_file_val(mdata_file, field);
    if (mdata_val) {
        if (user_set && mdata_val->type == _MDATA_FILE_VAL_TYPE_STRING) {
            return;
        }
    }
    else {
        _mdata_file_val_t val;
        memset(&val, 0, sizeof(val));
        vec_push(&mdata_file->vals, val);
        mdata_val = &vec_last(&mdata_file->vals);
    }

    mdata_val->type = _MDATA_FILE_VAL_TYPE_STRING;
    mdata_val->string_val = malloc(sizeof(char)*(strlen(string_val) + 1));
    strcpy(mdata_val->string_val, string_val);
    strncpy(mdata_val->name, field, _MDATA_VAL_MAX_NAME);
    mdata_val->name[_MDATA_VAL_MAX_NAME] = 0;
    mdata_val->user_set = user_set;
}

void mdata_file_add_val_binary_data(mdata_file_t *mdata_file, const char *field, char *data, int data_len, bool compress) {
    if (compress) {
        struct data_stream stream;
        data_stream_init(&stream);
        data_stream_push(&stream, data, data_len);
        data_stream_compress(&stream);

        _mdata_file_val_t val;
        val.type = _MDATA_FILE_VAL_TYPE_BIN_DATA;
        val.data_val.data = stream.data;
        val.data_val.len = stream.len;
        strncpy(val.name, field, _MDATA_VAL_MAX_NAME);
        val.name[_MDATA_VAL_MAX_NAME] = 0;
        val.user_set = false;
        vec_push(&mdata_file->vals, val);
    }
    else {
        _mdata_file_val_t val;
        val.type = _MDATA_FILE_VAL_TYPE_BIN_DATA;
        val.data_val.data = malloc(data_len);
        memcpy(val.data_val.data, data, data_len);
        val.data_val.len = data_len;
        strncpy(val.name, field, _MDATA_VAL_MAX_NAME);
        val.name[_MDATA_VAL_MAX_NAME] = 0;
        val.user_set = false;
        vec_push(&mdata_file->vals, val);
    }
}