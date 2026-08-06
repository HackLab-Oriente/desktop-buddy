// Declarations the ESP32 harness needs from the ported llama2.c run.c.
#pragma once
#include <stddef.h>

typedef struct { int dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, seq_len; } Config;
typedef struct { float *token_embedding_table, *rms_att_weight, *rms_ffn_weight,
                       *wq, *wk, *wv, *wo, *w1, *w2, *w3, *rms_final_weight, *wcls; } TransformerWeights;
typedef struct { float *x,*xb,*xb2,*hb,*hb2,*q,*k,*v,*att,*logits,*key_cache,*value_cache; } RunState;
typedef struct { Config config; TransformerWeights weights; RunState state;
                 int fd; float* data; long file_size; } Transformer;
typedef struct { char *str; int id; } TokenIndex;
typedef struct { char** vocab; float* vocab_scores; TokenIndex *sorted_vocab;
                 int vocab_size; unsigned int max_token_length; unsigned char byte_pieces[512]; } Tokenizer;
typedef struct { float prob; int index; } ProbIndex;
typedef struct { int vocab_size; ProbIndex* probindex; float temperature, topp;
                 unsigned long long rng_state; } Sampler;

void build_transformer(Transformer* t, char* checkpoint_path);
void build_tokenizer(Tokenizer* t, char* tokenizer_path, int vocab_size);
void build_sampler(Sampler* s, int vocab_size, float temperature, float topp, unsigned long long rng_seed);
float* forward(Transformer* transformer, int token, int pos);
int sample(Sampler* sampler, float* logits);
char* decode(Tokenizer* t, int prev_token, int token);
void relocate_weights_to_psram(Transformer* t, size_t bytes);
void prof_reset(void);
void prof_report(int n_tokens);
