#include "moarvm.h"

/* This file implements the various steps involved in getting 6model
 * bootstrapped from the ground up - that is, getting to having a
 * KnowHOW meta-object type so that userland can start building up
 * more interesting meta-objects. Mostly it just has to make objects
 * with some "holes", and later go back and fill them out. This is
 * due to the circular nature of things.
 */
 
/* Can do something better than statics later... */
static MVMString *str_repr     = NULL;
static MVMString *str_name     = NULL;
static MVMString *str_anon     = NULL;
static MVMString *str_P6opaque = NULL;

/* Creates a stub BOOTStr. Note we didn't initialize the
 * representation yet, so have to do this somewhat pokily. */
static void create_stub_BOOTStr(MVMThreadContext *tc) {
    /* Need to create the REPR function table "in advance"; the
     * MVMString REPR specially knows not to duplicately create
     * this. */
    MVMREPROps *repr = MVMString_initialize(tc);
    
    /* Now we can create a type object; note we have no HOW yet,
     * though. */
    MVMSTable *st  = MVM_gc_allocate_stable(tc, repr, NULL);
    
    /* We can now go for the type object. */
    tc->instance->boot_types->BOOTStr = MVM_gc_allocate_type_object(tc, st);
    
    /* Set the WHAT in the STable we just made to point to the type
     * object (this is completely normal). */
    st->WHAT = tc->instance->boot_types->BOOTStr;
}

/* Creates a stub BOOTArray (missing a meta-object). */
static void create_stub_BOOTArray(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMArray);
    tc->instance->boot_types->BOOTArray = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTHash (missing a meta-object). */
static void create_stub_BOOTHash(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMHash);
    tc->instance->boot_types->BOOTHash = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTCCode (missing a meta-object). */
static void create_stub_BOOTCCode(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMCFunction);
    tc->instance->boot_types->BOOTCCode = repr->type_object_for(tc, NULL);
}

/* KnowHOW.new_type method. Creates a new type with this HOW as its meta-object. */
static void new_type(MVMThreadContext *tc, MVMCallsite *callsite, MVMArg *args) {
    MVMObject  *self, *HOW, *type_object, *BOOTHash, *stash;
    MVMArg     *repr_arg, *name_arg;
    MVMString  *repr_name, *name;
    MVMREPROps *repr_to_use;
    
    /* Get arguments. */
    MVMArgProcContext arg_ctx;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    repr_arg = MVM_args_get_named_str(tc, &arg_ctx, str_repr, MVM_ARG_OPTIONAL);
    name_arg = MVM_args_get_named_str(tc, &arg_ctx, str_name, MVM_ARG_OPTIONAL);
    MVM_args_proc_cleanup(tc, &arg_ctx);
    
    /* We first create a new HOW instance. */
    HOW  = REPR(self)->allocate(tc, STABLE(self));
    
    /* See if we have a representation name; if not default to P6opaque. */
    repr_name = repr_arg ? repr_arg->s : str_P6opaque;
        
    /* Create a new type object of the desired REPR. (Note that we can't
     * default to KnowHOWREPR here, since it doesn't know how to actually
     * store attributes, it's just for bootstrapping knowhow's. */
    repr_to_use = MVM_repr_get_by_name(tc, repr_name);
    type_object = repr_to_use->type_object_for(tc, HOW);
    
    /* See if we were given a name; put it into the meta-object if so. */
    name = name_arg ? name_arg->s : str_anon;
    REPR(HOW)->initialize(tc, STABLE(HOW), HOW, OBJECT_BODY(HOW));
    MVM_WB(tc, HOW, name);
    ((MVMKnowHOWREPR *)HOW)->body.name = name;
    
    /* Set .WHO to an empty hash. */
    BOOTHash = tc->instance->boot_types->BOOTHash;
    stash = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
    REPR(stash)->initialize(tc, STABLE(stash), stash, OBJECT_BODY(stash));
    MVM_WB(tc, STABLE(type_object), stash);
    STABLE(type_object)->WHO = stash;

    /* Return the type object. */
    MVM_args_set_result_obj(tc, type_object);
}

/* Adds a method. */
static void add_method(MVMThreadContext *tc, MVMCallsite *callsite, MVMArg *args) {
    MVMObject *self, *type_obj, *method, *method_table;
    MVMString *name;
    
    /* Get arguments. */
    MVMArgProcContext arg_ctx;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    type_obj = MVM_args_get_pos_obj(tc, &arg_ctx, 1, MVM_ARG_REQUIRED)->o;
    name     = MVM_args_get_pos_str(tc, &arg_ctx, 2, MVM_ARG_REQUIRED)->s;
    method   = MVM_args_get_pos_obj(tc, &arg_ctx, 3, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    
    /* Add to method table. */
    method_table = ((MVMKnowHOWREPR *)self)->body.methods;
    REPR(method_table)->ass_funcs->bind_key_boxed(tc, STABLE(method_table),
        method_table, OBJECT_BODY(method_table), (MVMObject *)name, method);
    
    /* Return added method as result. */
    MVM_args_set_result_obj(tc, method);
}

/* Composes the meta-object. */
static void compose(MVMThreadContext *tc, MVMCallsite *callsite, MVMArg *args) {
    MVMObject *self, *type_obj, *method_table;
    
    /* Get arguments. */
    MVMArgProcContext arg_ctx;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    type_obj = MVM_args_get_pos_obj(tc, &arg_ctx, 1, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    
    /* Fill out STable. */
    method_table = ((MVMKnowHOWREPR *)self)->body.methods;
    WB(tc, STABLE(type_obj), method_table);
    WB(tc, STABLE(type_obj), type_obj);
    STABLE(type_obj)->method_cache            = method_table;
    STABLE(type_obj)->mode_flags              = MVM_METHOD_CACHE_AUTHORITATIVE;
    STABLE(type_obj)->type_check_cache_length = 1;
    STABLE(type_obj)->type_check_cache        = malloc(sizeof(MVMObject *));
    STABLE(type_obj)->type_check_cache[0]     = type_obj;
    
    /* Return type object. */
    MVM_args_set_result_obj(tc, type_obj);
}

/* Bootstraps the KnowHOW type. */
static void bootstrap_KnowHOW(MVMThreadContext *tc) {
    MVMObject *BOOTStr   = tc->instance->boot_types->BOOTStr;
    MVMObject *BOOTArray = tc->instance->boot_types->BOOTArray;
    MVMObject *BOOTHash  = tc->instance->boot_types->BOOTHash;
    
    /* Create our KnowHOW type object. Note we don't have a HOW just yet, so
     * pass in NULL. */
    MVMREPROps *REPR    = MVM_repr_get_by_id(tc, MVM_REPR_ID_KnowHOWREPR);
    MVMObject  *knowhow = REPR->type_object_for(tc, NULL);

    /* We create a KnowHOW instance that can describe itself. This means
     * (once we tie the knot) that .HOW.HOW.HOW.HOW etc will always return
     * that, which closes the model up. */
    MVMKnowHOWREPR *knowhow_how = (MVMKnowHOWREPR *)REPR->allocate(tc, NULL);
    
    /* Create an STable for the knowhow_how. */
    MVMSTable *st = MVM_gc_allocate_stable(tc, REPR, (MVMObject *)knowhow_how);
    st->WHAT = (MVMObject *)knowhow;
    knowhow_how->common.st = st;
    
    /* Add various methods to the KnowHOW's HOW. */
    REPR->initialize(tc, NULL, (MVMObject *)knowhow_how, &knowhow_how->body);
    /* XXX TODO: add the methods */
    
    /* Set name KnowHOW for the KnowHOW's HOW. */
    knowhow_how->body.name = MVM_string_ascii_decode_nt(tc, BOOTStr, "KnowHOW");

    /* Set this built up HOW as the KnowHOW's HOW. */
    STABLE(knowhow)->HOW = (MVMObject *)knowhow_how;
    
    /* Give it an authoritative method cache; this in turn will make the
     * method dispatch bottom out. */
    STABLE(knowhow)->method_cache = knowhow_how->body.methods;
    STABLE(knowhow)->mode_flags   = MVM_METHOD_CACHE_AUTHORITATIVE;
    
    /* Associate the created objects with the intial core serialization
     * context. */
    /* XXX TODO */

    /* Stash the created KnowHOW. */
    tc->instance->KnowHOW = (MVMObject *)knowhow;
}
 
/* Drives the overall bootstrap process. */
void MVM_6model_bootstrap(MVMThreadContext *tc) {
    /* First, we have to get the BOOTStr type to exist; this has to
     * come even before REPR registry setup because it relies on
     * being able to create strings. */
    create_stub_BOOTStr(tc);

    /* Now we've enough to actually create the REPR registry. */
    MVM_repr_initialize_registry(tc);

    /* Create stub BOOTArray, BOOTHash and BOOTCCode types. */
    create_stub_BOOTArray(tc);
    create_stub_BOOTHash(tc);
    create_stub_BOOTCCode(tc);

    /* Set up some strings. */
    str_repr     = MVM_string_ascii_decode_nt(tc, tc->instance->boot_types->BOOTStr, "repr");
    MVM_gc_root_add_pernament(tc, (MVMCollectable *)str_repr);
    str_name     = MVM_string_ascii_decode_nt(tc, tc->instance->boot_types->BOOTStr, "name");
    MVM_gc_root_add_pernament(tc, (MVMCollectable *)str_name);
    str_anon     = MVM_string_ascii_decode_nt(tc, tc->instance->boot_types->BOOTStr, "<anon>");
    MVM_gc_root_add_pernament(tc, (MVMCollectable *)str_anon);
    str_P6opaque = MVM_string_ascii_decode_nt(tc, tc->instance->boot_types->BOOTStr, "P6opaque");
    MVM_gc_root_add_pernament(tc, (MVMCollectable *)str_P6opaque);
    
    /* Bootstrap the KnowHOW type, giving it a meta-object. */
    bootstrap_KnowHOW(tc);
    
    /* XXX Give BOOTStr, BOOTArray, BOOTHash and BOOTCode meta-objects... */
}
