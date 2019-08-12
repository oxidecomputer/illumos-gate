 * Copyright (c) 2018, Joyent, Inc.
 * ----------	----------	----------	----------
	if (uap->fname == 0)
	if (uap->fname == 0)
	if (fd == AT_FDCWD || uap->fname != 0)	/* same as chmod() */
	if (uap->fnamep == 0)
{
	/* AUS_START */
		if (copyin((caddr_t)dataptr, STRUCT_BUF(nfsargs),
		    MIN(uap->datalen, STRUCT_SIZE(nfsargs)))) {
		    (caddr_t)hostname, MAXNAMELEN, &len)) {
		    (uint_t)STRUCT_FGET(nfsargs, flags)));
	case 4: /* getpgid()	- not security relevant */
		au_uwrite(au_to_arg32(3, "arg", (uint32_t)cmarg));
		au_uwrite(au_to_arg64(3, "arg", (uint64_t)cmarg));
	au_uwrite(au_to_arg32(3, "arg", (uint32_t)cmarg));
	au_uwrite(au_to_arg64(3, "arg", (uint64_t)cmarg));
		case A_GETPINFO:
		case A_GETPINFO_ADDR:
			e = AUE_AUDITON_GETPINFO;
			break;
		case A_SETPMASK:
			e = AUE_AUDITON_SETPMASK;
			break;
		case A_GETKAUDIT:
			e = AUE_AUDITON_GETKAUDIT;
			break;
		case A_SETKAUDIT:
			e = AUE_AUDITON_SETKAUDIT;
			break;
			e = AUE_AUDITON_OTHER;
	STRUCT_DECL(auditpinfo, apinfo);
	case AUE_AUDITON_SETPMASK:
		STRUCT_INIT(apinfo, get_udatamodel());
		if (copyin((caddr_t)uap->a2, STRUCT_BUF(apinfo),
		    STRUCT_SIZE(apinfo))) {
			return;
		}
		au_uwrite(au_to_arg32(3, "setpmask:pid",
		    (uint32_t)STRUCT_FGET(apinfo, ap_pid)));
		au_uwrite(au_to_arg32(3, "setpmask:as_success",
		    (uint32_t)STRUCT_FGET(apinfo, ap_mask.as_success)));
		au_uwrite(au_to_arg32(3, "setpmask:as_failure",
		    (uint32_t)STRUCT_FGET(apinfo, ap_mask.as_failure)));
		break;
	case AUE_AUDITON_SETKAUDIT:
		STRUCT_INIT(ainfo_addr, get_udatamodel());
		if (copyin((caddr_t)a1, STRUCT_BUF(ainfo_addr),
		    STRUCT_SIZE(ainfo_addr))) {
				return;
		}
		au_uwrite(au_to_arg32((char)1, "auid",
		    (uint32_t)STRUCT_FGET(ainfo_addr, ai_auid)));
#ifdef _LP64
		au_uwrite(au_to_arg64((char)1, "port",
		    (uint64_t)STRUCT_FGET(ainfo_addr, ai_termid.at_port)));
#else
		au_uwrite(au_to_arg32((char)1, "port",
		    (uint32_t)STRUCT_FGET(ainfo_addr, ai_termid.at_port)));
#endif
		au_uwrite(au_to_arg32((char)1, "type",
		    (uint32_t)STRUCT_FGET(ainfo_addr, ai_termid.at_type)));
		if ((uint32_t)STRUCT_FGET(ainfo_addr, ai_termid.at_type) ==
		    AU_IPv4) {
			au_uwrite(au_to_in_addr(
			    (struct in_addr *)STRUCT_FGETP(ainfo_addr,
			    ai_termid.at_addr)));
		} else {
			au_uwrite(au_to_in_addr_ex(
			    (int32_t *)STRUCT_FGETP(ainfo_addr,
			    ai_termid.at_addr)));
		}
		au_uwrite(au_to_arg32((char)1, "as_success",
		    (uint32_t)STRUCT_FGET(ainfo_addr, ai_mask.as_success)));
		au_uwrite(au_to_arg32((char)1, "as_failure",
		    (uint32_t)STRUCT_FGET(ainfo_addr, ai_mask.as_failure)));
		au_uwrite(au_to_arg32((char)1, "asid",
		    (uint32_t)STRUCT_FGET(ainfo_addr, ai_asid)));
		break;
	case AUE_AUDITON_GETPINFO:
	case AUE_AUDITON_GETKAUDIT:
	case AUE_AUDITON_OTHER:
			if (uap->addr == 0)
aus_sockconfig(struct t_audit_data *tad)
			if (uap->from == 0)
			if (uap->to == 0)
auf_read(struct t_audit_data *tad, int error, rval_t *rval)
auf_write(struct t_audit_data *tad, int error, rval_t *rval)
auf_recv(struct t_audit_data *tad, int error, rval_t *rval)
auf_send(struct t_audit_data *tad, int error, rval_t *rval)