#include <bqnffi.h>
#include <erl_nif.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ERL_NIF_TERM ok_atom;
ERL_NIF_TERM tsdiff_atom;
ErlNifResourceType* BEAMQN_BQNV;

static ERL_NIF_TERM beamqn_make_atom(ErlNifEnv* env, const char* atom) {
    ERL_NIF_TERM ret;

    if(!enif_make_existing_atom(env, atom, &ret, ERL_NIF_LATIN1)) {
        return enif_make_atom(env, atom);
    }

    return ret;
}

static void beamqn_free_bqnv(ErlNifEnv* env, void* ptr) {
    BQNV* x = (BQNV*) ptr;
    // CBQN uses its own memory management system (see CBQN/src/opt/) and reads past the end
    // of allocations (see CBQN/src/opt/mm_malloc.c).
    // It is unknown if allocations made with enif_alloc allow reading past the end.
    // If we use the erts allocator, and it does not allow reading past the end, we may
    // need to allocate an additional 64 bytes on every enif_alloc.
    // To use the erts allocator, we will need to either add a new allocator to CBQN,
    // or overload the malloc/free calls with erts equivalents.
    bqn_free(*x);
}

static int beamqn_init(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info) {
    ok_atom = beamqn_make_atom(env, "ok");
    tsdiff_atom = beamqn_make_atom(env, "tsdiff");
    BEAMQN_BQNV = enif_open_resource_type(env, NULL, "BQNV", beamqn_free_bqnv, ERL_NIF_RT_CREATE|ERL_NIF_RT_TAKEOVER, NULL);
    bqn_init();
    return 0;
}

typedef struct BqnMakeOpt { bool tsdiff; } BqnMakeOpt;
typedef struct BqnMakeStat { size_t count; ERL_NIF_TERM keys[4]; ERL_NIF_TERM values[4]; } BqnMakeStat;

static ERL_NIF_TERM beamqn_bqn_make(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {

    BqnMakeOpt opt;
    BqnMakeStat stat;

    stat.count = 0;
    opt.tsdiff = false;

    if (argc != 1 && argc != 2) {
        return enif_make_badarg(env);
    }
    if (argc == 2) {
        unsigned w_len, opt_len;
        ERL_NIF_TERM w, w_hd;
        int w_arity;
        const ERL_NIF_TERM* w_cur;
        char opt_buf[32];

        w = argv[1];

        if(!enif_is_list(env, w)) {
            return enif_make_badarg(env);
        }
        if (!enif_get_list_length(env, w, &w_len)) {
            return enif_make_badarg(env);
        }

        for (int i = 0; enif_get_list_cell(env, w, &w_hd, (ERL_NIF_TERM*) &w); i++) {
            if (!enif_get_tuple(env, w_hd, &w_arity, &w_cur)) {
                return enif_make_badarg(env);
            }
            if (w_arity != 2) {
                return enif_make_badarg(env);
            }
            if(!enif_is_atom(env, w_cur[0])) {
                return enif_make_badarg(env);
            }

            opt_len = enif_get_atom(env, w_cur[0], opt_buf, sizeof(opt_buf), ERL_NIF_LATIN1);

            if (opt_len == 1 || opt_len == 0 || opt_len > 32) {
                return enif_make_badarg(env);
            }

            if (strcmp(opt_buf, "tsdiff") == 0) {
                opt_len = enif_get_atom(env, w_cur[1], opt_buf, sizeof(opt_buf), ERL_NIF_LATIN1);

                if (opt_len == 1 || opt_len == 0 || opt_len > 32) {
                    return enif_make_badarg(env);
                }

                if (strcmp(opt_buf, "true") == 0) {
                    opt.tsdiff = true;
                }
                else if (strcmp(opt_buf, "false") == 0) {
                    opt.tsdiff = false;
                }
                else {
                    return enif_make_badarg(env);
                }
            }
            else {
                return enif_make_badarg(env);
            }
        }
    }

    double ts0;

    if (opt.tsdiff) {
        ts0 = enif_monotonic_time(ERL_NIF_USEC);
    }

    BQNV* bqnv;
    ERL_NIF_TERM ref;
    switch (enif_term_type(env,argv[0])) {
        case ERL_NIF_TERM_TYPE_ATOM:
            return enif_make_badarg(env);
            break;
        case ERL_NIF_TERM_TYPE_BITSTRING:
            return enif_make_badarg(env);
            break;
        case ERL_NIF_TERM_TYPE_FLOAT:
            double f64_val;
            if (!enif_get_double(env, argv[0], &f64_val)) {
                return enif_make_badarg(env);
            }
            bqnv = enif_alloc_resource(BEAMQN_BQNV, sizeof(BQNV));
            *bqnv = bqn_makeF64(f64_val);
            ref = enif_make_resource(env, bqnv);
            enif_release_resource(bqnv);
            break;
        case ERL_NIF_TERM_TYPE_FUN:
            return enif_make_badarg(env);
            break;
        case ERL_NIF_TERM_TYPE_INTEGER:
            return enif_make_badarg(env);
            break;
        case ERL_NIF_TERM_TYPE_LIST:
            ERL_NIF_TERM x, x_hd;
            double x_cur;
            unsigned x_len;

            x = argv[0];
            if (!enif_get_list_length(env, argv[0], &x_len)) {
                return enif_make_badarg(env);
            }

            double* f64_vec_val = enif_alloc(x_len * sizeof(double));
            bqnv = enif_alloc_resource(BEAMQN_BQNV, sizeof(BQNV));

            for (int i = 0; enif_get_list_cell(env,x,&x_hd,(ERL_NIF_TERM*) &x); i++) {
                if (!enif_get_double(env, x_hd, &x_cur)) {
                    return enif_make_badarg(env);
                }
                f64_vec_val[i] = x_cur;
            }
            *bqnv = bqn_makeF64Vec(x_len, f64_vec_val);

            ref = enif_make_resource(env, bqnv);
            enif_release_resource(bqnv);
            break;
        case ERL_NIF_TERM_TYPE_MAP:
            return enif_make_badarg(env);
            break;
        case ERL_NIF_TERM_TYPE_PID:
            return enif_make_badarg(env);
            break;
        case ERL_NIF_TERM_TYPE_PORT:
            return enif_make_badarg(env);
            break;
        case ERL_NIF_TERM_TYPE_REFERENCE:
            return enif_make_badarg(env);
            break;
        case ERL_NIF_TERM_TYPE_TUPLE:
            return enif_make_badarg(env);
            break;
        default:
            return enif_make_badarg(env);
            break;
    }

    if (opt.tsdiff) {
        stat.keys[stat.count] = tsdiff_atom;
        stat.values[stat.count] = enif_make_int64(env, enif_monotonic_time(ERL_NIF_USEC)-ts0);
        stat.count++;
    }

    ERL_NIF_TERM rtn;
    if (argc == 1) {
        rtn = enif_make_tuple2(env, ok_atom, ref);
    }
    else if (argc == 2) {
        ERL_NIF_TERM stat_out;
        if (!enif_make_map_from_arrays(env, stat.keys, stat.values, stat.count, &stat_out)) {
            return enif_make_badarg(env);
        }
        rtn = enif_make_tuple3(env, ok_atom, ref, stat_out);
    }
    else {
        return enif_make_badarg(env);
    }
    return rtn;
}

typedef struct BqnReadOpt { bool tsdiff; } BqnReadOpt;
typedef struct BqnReadStat { size_t count; ERL_NIF_TERM keys[4]; ERL_NIF_TERM values[4]; } BqnReadStat;

static ERL_NIF_TERM beamqn_bqn_read(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    BQNV* bqnv;
    ERL_NIF_TERM term;
    BqnReadOpt opt;
    BqnReadStat stat;

    stat.count = 0;
    opt.tsdiff = false;

    if (argc != 1 && argc != 2) {
        return enif_make_badarg(env);
    }
    if (argc == 2) {
        unsigned w_len, opt_len;
        ERL_NIF_TERM w, w_hd;
        int w_arity;
        const ERL_NIF_TERM* w_cur;
        char opt_buf[32];

        w = argv[1];

        if(!enif_is_list(env, w)) {
            return enif_make_badarg(env);
        }
        if (!enif_get_list_length(env, w, &w_len)) {
            return enif_make_badarg(env);
        }

        for (int i = 0; enif_get_list_cell(env, w, &w_hd, (ERL_NIF_TERM*) &w); i++) {
            if (!enif_get_tuple(env, w_hd, &w_arity, &w_cur)) {
                return enif_make_badarg(env);
            }
            if (w_arity != 2) {
                return enif_make_badarg(env);
            }
            if(!enif_is_atom(env, w_cur[0])) {
                return enif_make_badarg(env);
            }

            opt_len = enif_get_atom(env, w_cur[0], opt_buf, sizeof(opt_buf), ERL_NIF_LATIN1);

            if (opt_len == 1 || opt_len == 0 || opt_len > 32) {
                return enif_make_badarg(env);
            }

            if (strcmp(opt_buf, "tsdiff") == 0) {
                opt_len = enif_get_atom(env, w_cur[1], opt_buf, sizeof(opt_buf), ERL_NIF_LATIN1);

                if (opt_len == 1 || opt_len == 0 || opt_len > 32) {
                    return enif_make_badarg(env);
                }

                if (strcmp(opt_buf, "true") == 0) {
                    opt.tsdiff = true;
                }
                else if (strcmp(opt_buf, "false") == 0) {
                    opt.tsdiff = false;
                }
                else {
                    return enif_make_badarg(env);
                }
            }
            else {
                return enif_make_badarg(env);
            }
        }
    }

    double ts0;

    if (opt.tsdiff) {
        ts0 = enif_monotonic_time(ERL_NIF_USEC);
    }

    if (!enif_get_resource(env, argv[0], BEAMQN_BQNV, (void**) &bqnv)) {
        return enif_make_badarg(env);
    }

    switch (bqn_type(*bqnv)) {
        case 0: // array
            size_t len = bqn_bound(*bqnv);
            if (len == 0) {
                term = enif_make_list(env,0);
            }
            else {
                double* buf = enif_alloc(len * sizeof(double));
                bqn_readF64Arr(*bqnv, buf);

                ERL_NIF_TERM* ebuf = enif_alloc(len * sizeof(ERL_NIF_TERM));
                for (int i = 0; i < len; i++) {
                    ebuf[i] = enif_make_double(env,buf[i]);
                }

                term = enif_make_list_from_array(env,ebuf,len);

                enif_free(buf);
                enif_free(ebuf);
            }
            break;
        case 1: // number
            term = enif_make_double(env, bqn_readF64(*bqnv));
            break;
        default:
            return enif_make_badarg(env);
            break;
    }

    if (opt.tsdiff) {
        stat.keys[stat.count] = tsdiff_atom;
        stat.values[stat.count] = enif_make_int64(env, enif_monotonic_time(ERL_NIF_USEC)-ts0);
        stat.count++;
    }

    ERL_NIF_TERM rtn;
    if (argc == 1) {
        rtn = enif_make_tuple2(env, ok_atom, term);
    }
    else if (argc == 2) {
        ERL_NIF_TERM stat_out;
        if (!enif_make_map_from_arrays(env, stat.keys, stat.values, stat.count, &stat_out)) {
            return enif_make_badarg(env);
        }
        rtn = enif_make_tuple3(env, ok_atom, term, stat_out);
    }
    else {
        return enif_make_badarg(env);
    }
    return rtn;
}

static ErlNifFunc nif_funcs[] = {
    {"make",    1, beamqn_bqn_make,ERL_NIF_DIRTY_JOB_CPU_BOUND},
    {"make",    2, beamqn_bqn_make,ERL_NIF_DIRTY_JOB_CPU_BOUND},
    {"read",    1, beamqn_bqn_read,ERL_NIF_DIRTY_JOB_CPU_BOUND},
    {"read",    2, beamqn_bqn_read,ERL_NIF_DIRTY_JOB_CPU_BOUND}
};

ERL_NIF_INIT(beamqn, nif_funcs, &beamqn_init, NULL, NULL, NULL)
