#include "ldparser.h"
#include <stdint.h>
#include <math.h>

// Helper function to decode strings (remove trailing zeros)
static void decode_string(char* dest, const char* src, size_t max_len) {
    strncpy(dest, src, max_len - 1);
    dest[max_len - 1] = '\0';
    
    // Remove trailing zeros and whitespace
    size_t len = strlen(dest);
    while (len > 0 && (dest[len-1] == '\0' || dest[len-1] == ' ')) {
        dest[--len] = '\0';
    }
}

// Read channel data
static void* read_channel_data(FILE* f, LDChannel* chan) {
    void* data = NULL;
    size_t element_size;
    
    switch(chan->dtype) {
        case DTYPE_FLOAT32:
            element_size = sizeof(float);
            break;
        case DTYPE_INT32:
            element_size = sizeof(int32_t);
            break;
        case DTYPE_FLOAT16:
        case DTYPE_INT16:
            element_size = sizeof(int16_t);
            break;
        default:
            return NULL;
    }
    
    data = malloc(chan->data_len * element_size);
    if (!data) return NULL;
    
    fseek(f, chan->data_ptr, SEEK_SET);
    fread(data, element_size, chan->data_len, f);
    
    // Apply scaling factors
    if (chan->dtype == DTYPE_FLOAT32) {
        float* fdata = (float*)data;
        for (int i = 0; i < chan->data_len; i++) {
            fdata[i] = (fdata[i] / chan->scale * pow(10, -chan->dec) + chan->shift) * chan->mul;
        }
    }
    
    return data;
}

// Read a single channel
static LDChannel* read_channel(FILE* f, int meta_ptr) {
    LDChannel* chan = malloc(sizeof(LDChannel));
    if (!chan) return NULL;
    
    fseek(f, meta_ptr, SEEK_SET);
    
    // Read channel header
    fread(&chan->prev_meta_ptr, sizeof(int), 1, f);
    fread(&chan->next_meta_ptr, sizeof(int), 1, f);
    fread(&chan->data_ptr, sizeof(int), 1, f);
    fread(&chan->data_len, sizeof(int), 1, f);
    
    // Read other channel metadata
    uint16_t dtype_a, dtype;
    fread(&dtype_a, sizeof(uint16_t), 1, f);
    fread(&dtype, sizeof(uint16_t), 1, f);
    
    // Determine data type
    if (dtype_a == 0x07) {
        chan->dtype = (dtype == 2) ? DTYPE_FLOAT16 : DTYPE_FLOAT32;
    } else {
        chan->dtype = (dtype == 2) ? DTYPE_INT16 : DTYPE_INT32;
    }
    
    // Read remaining metadata
    fread(&chan->freq, sizeof(int16_t), 1, f);
    fread(&chan->shift, sizeof(int16_t), 1, f);
    fread(&chan->mul, sizeof(int16_t), 1, f);
    fread(&chan->scale, sizeof(int16_t), 1, f);
    fread(&chan->dec, sizeof(int16_t), 1, f);
    
    // Read strings
    char temp[32];
    fread(temp, 32, 1, f);
    decode_string(chan->name, temp, sizeof(chan->name));
    
    fread(temp, 8, 1, f);
    decode_string(chan->short_name, temp, sizeof(chan->short_name));
    
    fread(temp, 12, 1, f);
    decode_string(chan->unit, temp, sizeof(chan->unit));
    
    // Read actual channel data
    chan->data = read_channel_data(f, chan);
    
    return chan;
}

LDData* ld_read_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    
    LDData* data = malloc(sizeof(LDData));
    if (!data) {
        fclose(f);
        return NULL;
    }
    
    // Read header
    data->head = malloc(sizeof(LDHeader));
    if (!data->head) {
        free(data);
        fclose(f);
        return NULL;
    }
    
    // Read channels
    data->channels = malloc(sizeof(LDChannel*) * MAX_CHANNELS);
    data->channel_count = 0;
    
    int meta_ptr = data->head->meta_ptr;
    while (meta_ptr && data->channel_count < MAX_CHANNELS) {
        LDChannel* chan = read_channel(f, meta_ptr);
        if (!chan) break;
        
        data->channels[data->channel_count++] = chan;
        meta_ptr = chan->next_meta_ptr;
    }
    
    fclose(f);
    return data;
}

// Free all allocated memory
void ld_free_data(LDData* data) {
    if (!data) return;
    
    if (data->head) {
        if (data->head->aux) {
            if (data->head->aux->venue) {
                if (data->head->aux->venue->vehicle) {
                    free(data->head->aux->venue->vehicle);
                }
                free(data->head->aux->venue);
            }
            free(data->head->aux);
        }
        free(data->head);
    }
    
    for (int i = 0; i < data->channel_count; i++) {
        if (data->channels[i]) {
            if (data->channels[i]->data) {
                free(data->channels[i]->data);
            }
            free(data->channels[i]);
        }
    }
    
    free(data->channels);
    free(data);
}