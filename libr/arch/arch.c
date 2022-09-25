/* radare - LGPL - Copyright 2022 - pancake, condret */

#include <r_arch.h>
#include <config.h>

static const RArchPlugin * const arch_static_plugins[] = { R_ARCH_STATIC_PLUGINS };

static void plugin_free (void *p) {
}

static void _decoder_free_cb (HtPPKv *kv) {
	free (kv->key);
	RArchDecoder *decoder = (RArchDecoder *)kv->value;
	if (decoder->p->fini) {
		decoder->p->fini (decoder->user);
	}
	free (decoder);
}

R_API RArch *r_arch_new(void) {
	RArch *a = R_NEW0 (RArch);
	if (!a) {
		return NULL;
	}
	a->plugins = r_list_newf ((RListFree)plugin_free);
	if (!a->plugins) {
		free (a);
		return NULL;
	}
	a->decoders = ht_pp_new (NULL, _decoder_free_cb, NULL);
	if (!a->decoders) {
		r_list_free (a->plugins);
		free (a);
		return NULL;
	}
	ut32 i = 0;
	while (arch_static_plugins[i]) {
		r_arch_add (a, (RArchPlugin*)arch_static_plugins[i++]);
	}
	return a;
}

static ut32 _rate_compat(RArchPlugin *p, RArchConfig *cfg) {
	ut32 bits;
	switch (cfg->bits) {
	case 64:
		bits = R_SYS_BITS_64;
		break;
	case 32:
		bits = R_SYS_BITS_32;
		break;
	case 27:
		bits = R_SYS_BITS_27;
		break;
	case 16:
		bits = R_SYS_BITS_16;
		break;
	case 12:
		bits = R_SYS_BITS_12;
		break;
	case 8:
		bits = R_SYS_BITS_8;
		break;
	case 4:
		bits = R_SYS_BITS_4;
		break;
	default:
		bits = UT32_MAX;
	}
	ut32 score = 0;
	if (!strcmp (p->arch, cfg->arch)) {
		score = 50;
	}
	if (p->bits & bits) {
		score += (!!score) * 30;
	}
	if (p->endian & cfg->endian) {
		score += (!!score) * 20;
	}
	return score;
}

static char *_find_bestmatch(RList *plugins, RArchConfig *cfg) {
	ut8 best_score = 0;
	char *name = NULL;
	RListIter *iter;
	RArchPlugin *p;
	r_list_foreach (plugins, iter, p) {
		const ut32 score = _rate_compat (p, cfg);
		if (score > best_score) {
			best_score = score;
			name = p->name;
		}
		if (score == 100) {
			break;
		}
	}
	return name;
}

R_API bool r_arch_use(RArch *arch, RArchConfig *config) {
	r_return_val_if_fail (arch && config && (config->arch || config->decoder), false);
	const char *dname = config->decoder ? config->decoder: _find_bestmatch (arch->plugins, config);
	if (!dname) {
		return false;
	}
	r_ref (config);
	if (!r_arch_use_decoder (arch, dname)) {
		r_unref (config);
		return false;
	}
	r_unref (arch->cfg);
	arch->cfg = config;
	if (!config->decoder) {
		config->decoder = strdup (arch->current->p->name);
	}
	return true;
}

R_API bool r_arch_set_bits(RArch *arch, ut32 bits) {
	r_return_val_if_fail (arch && bits, false);
	if (!arch->cfg) {
		arch->cfg = r_arch_config_new ();
		if (!arch->cfg) {
			return false;
		}
		arch->cfg->bits = bits;
		if (!r_arch_use (arch, arch->cfg)) {
			r_unref (arch->cfg);
			arch->cfg = NULL;
			return false;
		}
	}
	if (arch->autoselect) {
		if (arch->current) {
			const ut32 score = _rate_compat (arch->current->p, arch->cfg);
			arch->cfg->bits = bits;
			if (!score || score > _rate_compat (arch->current->p, arch->cfg)) {
				return r_arch_use (arch, arch->cfg);
			}
			return true;
		}
		arch->cfg->bits = bits;
		return r_arch_use (arch, arch->cfg);
	}
	arch->cfg->bits = bits;
	return true;
}

R_API bool r_arch_set_endian(RArch *arch, ut32 endian) {
	r_return_val_if_fail (arch, false);
	if (!arch->cfg) {
		arch->cfg = r_arch_config_new ();
		if (!arch->cfg) {
			return false;
		}
		arch->cfg->endian = endian;
		if (!r_arch_use (arch, arch->cfg)) {
			r_unref (arch->cfg);
			arch->cfg = NULL;
			return false;
		}
	}
	if (arch->autoselect) {
		if (arch->current) {
			const ut32 score = _rate_compat (arch->current->p, arch->cfg);
			arch->cfg->endian = endian;
			if (!score || score > _rate_compat (arch->current->p, arch->cfg)) {
				return r_arch_use (arch, arch->cfg);
			}
			return true;
		}
		arch->cfg->endian = endian;
		return r_arch_use (arch, arch->cfg);
	}
	arch->cfg->bits = endian;
	return true;
}

R_API bool r_arch_set_arch(RArch *arch, char *archname) {
	r_return_val_if_fail (arch && archname, false);
	char *_arch = strdup (archname);
	if (!_arch) {
		return false;
	}
	if (!arch->cfg) {
		arch->cfg = r_arch_config_new ();
		if (!arch->cfg) {
			free (_arch);
			return false;
		}
		arch->cfg->arch = _arch;
		if (!r_arch_use (arch, arch->cfg)) {
			r_unref (arch->cfg);
			arch->cfg = NULL;
			return false;
		}
	}
	if (arch->autoselect) {
		if (arch->current) {
			const ut32 score = _rate_compat (arch->current->p, arch->cfg);
			free (arch->cfg->arch);
			arch->cfg->arch = _arch;
			if (!score || score > _rate_compat (arch->current->p, arch->cfg)) {
				return r_arch_use (arch, arch->cfg);
			}
			return true;
		}
		free (arch->cfg->arch);
		arch->cfg->arch = _arch;
		return r_arch_use (arch, arch->cfg);
	}
	free (arch->cfg->arch);
	arch->cfg->arch = _arch;
	return true;
}

R_API bool r_arch_add(RArch *a, RArchPlugin *ap) {
	r_return_val_if_fail (a && ap->name && ap->arch, false);
	return !!r_list_append (a->plugins, ap);
}

static bool _pick_any_decoder_as_current (void *user, const char *dname, const void *dec) {
	RArch *arch = (RArch *)user;
	arch->current = (RArchDecoder *)dec;
	return false;
}

R_API bool r_arch_del(RArch *a, const char *name) {
	r_return_val_if_fail (a && a->plugins && name, false);
	if (a->current && !strcmp (a->current->p->name, name)) {
		a->current = NULL;
	}
	if (a->decoders) {
		ht_pp_delete (a->decoders, name);
	}
	RListIter *iter;
	RArchPlugin *p;
	r_list_foreach (a->plugins, iter, p) {
		if (!strcmp (name, p->name)) {
			r_list_delete (a->plugins, iter);
			if (!a->current) {
				ht_pp_foreach (a->decoders, (HtPPForeachCallback)_pick_any_decoder_as_current, a);
			}
			return true;
		}
	}
	return false;
}

R_API void r_arch_free(RArch *a) {
	r_return_if_fail (a);
	ht_pp_free (a->decoders);
	r_list_free (a->plugins);
	if (a->cfg) {
		r_unref (a->cfg);
	}
	free (a);
}
