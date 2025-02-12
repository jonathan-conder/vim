/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * misc1.c: functions that didn't seem to fit elsewhere
 */

#include "vim.h"
#include "version.h"

#if defined(MSWIN)
# include <lm.h>
#endif

#define URL_SLASH	1		/* path_is_url() has found "://" */
#define URL_BACKSLASH	2		/* path_is_url() has found ":\\" */

// All user names (for ~user completion as done by shell).
static garray_T	ga_users;

/*
 * Count the size (in window cells) of the indent in the current line.
 */
    int
get_indent(void)
{
#ifdef FEAT_VARTABS
    return get_indent_str_vtab(ml_get_curline(), (int)curbuf->b_p_ts,
						 curbuf->b_p_vts_array, FALSE);
#else
    return get_indent_str(ml_get_curline(), (int)curbuf->b_p_ts, FALSE);
#endif
}

/*
 * Count the size (in window cells) of the indent in line "lnum".
 */
    int
get_indent_lnum(linenr_T lnum)
{
#ifdef FEAT_VARTABS
    return get_indent_str_vtab(ml_get(lnum), (int)curbuf->b_p_ts,
						 curbuf->b_p_vts_array, FALSE);
#else
    return get_indent_str(ml_get(lnum), (int)curbuf->b_p_ts, FALSE);
#endif
}

#if defined(FEAT_FOLDING) || defined(PROTO)
/*
 * Count the size (in window cells) of the indent in line "lnum" of buffer
 * "buf".
 */
    int
get_indent_buf(buf_T *buf, linenr_T lnum)
{
#ifdef FEAT_VARTABS
    return get_indent_str_vtab(ml_get_buf(buf, lnum, FALSE),
			       (int)curbuf->b_p_ts, buf->b_p_vts_array, FALSE);
#else
    return get_indent_str(ml_get_buf(buf, lnum, FALSE), (int)buf->b_p_ts, FALSE);
#endif
}
#endif

/*
 * count the size (in window cells) of the indent in line "ptr", with
 * 'tabstop' at "ts"
 */
    int
get_indent_str(
    char_u	*ptr,
    int		ts,
    int		list) /* if TRUE, count only screen size for tabs */
{
    int		count = 0;

    for ( ; *ptr; ++ptr)
    {
	if (*ptr == TAB)
	{
	    if (!list || lcs_tab1)    /* count a tab for what it is worth */
		count += ts - (count % ts);
	    else
		/* In list mode, when tab is not set, count screen char width
		 * for Tab, displays: ^I */
		count += ptr2cells(ptr);
	}
	else if (*ptr == ' ')
	    ++count;		/* count a space for one */
	else
	    break;
    }
    return count;
}

#ifdef FEAT_VARTABS
/*
 * Count the size (in window cells) of the indent in line "ptr", using
 * variable tabstops.
 * if "list" is TRUE, count only screen size for tabs.
 */
    int
get_indent_str_vtab(char_u *ptr, int ts, int *vts, int list)
{
    int		count = 0;

    for ( ; *ptr; ++ptr)
    {
	if (*ptr == TAB)    /* count a tab for what it is worth */
	{
	    if (!list || lcs_tab1)
		count += tabstop_padding(count, ts, vts);
	    else
		/* In list mode, when tab is not set, count screen char width
		 * for Tab, displays: ^I */
		count += ptr2cells(ptr);
	}
	else if (*ptr == ' ')
	    ++count;		/* count a space for one */
	else
	    break;
    }
    return count;
}
#endif

/*
 * Set the indent of the current line.
 * Leaves the cursor on the first non-blank in the line.
 * Caller must take care of undo.
 * "flags":
 *	SIN_CHANGED:	call changed_bytes() if the line was changed.
 *	SIN_INSERT:	insert the indent in front of the line.
 *	SIN_UNDO:	save line for undo before changing it.
 * Returns TRUE if the line was changed.
 */
    int
set_indent(
    int		size,		    /* measured in spaces */
    int		flags)
{
    char_u	*p;
    char_u	*newline;
    char_u	*oldline;
    char_u	*s;
    int		todo;
    int		ind_len;	    /* measured in characters */
    int		line_len;
    int		doit = FALSE;
    int		ind_done = 0;	    /* measured in spaces */
#ifdef FEAT_VARTABS
    int		ind_col = 0;
#endif
    int		tab_pad;
    int		retval = FALSE;
    int		orig_char_len = -1; /* number of initial whitespace chars when
				       'et' and 'pi' are both set */

    /*
     * First check if there is anything to do and compute the number of
     * characters needed for the indent.
     */
    todo = size;
    ind_len = 0;
    p = oldline = ml_get_curline();

    /* Calculate the buffer size for the new indent, and check to see if it
     * isn't already set */

    /* if 'expandtab' isn't set: use TABs; if both 'expandtab' and
     * 'preserveindent' are set count the number of characters at the
     * beginning of the line to be copied */
    if (!curbuf->b_p_et || (!(flags & SIN_INSERT) && curbuf->b_p_pi))
    {
	/* If 'preserveindent' is set then reuse as much as possible of
	 * the existing indent structure for the new indent */
	if (!(flags & SIN_INSERT) && curbuf->b_p_pi)
	{
	    ind_done = 0;

	    /* count as many characters as we can use */
	    while (todo > 0 && VIM_ISWHITE(*p))
	    {
		if (*p == TAB)
		{
#ifdef FEAT_VARTABS
		    tab_pad = tabstop_padding(ind_done, curbuf->b_p_ts,
							curbuf->b_p_vts_array);
#else
		    tab_pad = (int)curbuf->b_p_ts
					   - (ind_done % (int)curbuf->b_p_ts);
#endif
		    /* stop if this tab will overshoot the target */
		    if (todo < tab_pad)
			break;
		    todo -= tab_pad;
		    ++ind_len;
		    ind_done += tab_pad;
		}
		else
		{
		    --todo;
		    ++ind_len;
		    ++ind_done;
		}
		++p;
	    }

#ifdef FEAT_VARTABS
	    /* These diverge from this point. */
	    ind_col = ind_done;
#endif
	    /* Set initial number of whitespace chars to copy if we are
	     * preserving indent but expandtab is set */
	    if (curbuf->b_p_et)
		orig_char_len = ind_len;

	    /* Fill to next tabstop with a tab, if possible */
#ifdef FEAT_VARTABS
	    tab_pad = tabstop_padding(ind_done, curbuf->b_p_ts,
						curbuf->b_p_vts_array);
#else
	    tab_pad = (int)curbuf->b_p_ts - (ind_done % (int)curbuf->b_p_ts);
#endif
	    if (todo >= tab_pad && orig_char_len == -1)
	    {
		doit = TRUE;
		todo -= tab_pad;
		++ind_len;
		/* ind_done += tab_pad; */
#ifdef FEAT_VARTABS
		ind_col += tab_pad;
#endif
	    }
	}

	/* count tabs required for indent */
#ifdef FEAT_VARTABS
	for (;;)
	{
	    tab_pad = tabstop_padding(ind_col, curbuf->b_p_ts,
							curbuf->b_p_vts_array);
	    if (todo < tab_pad)
		break;
	    if (*p != TAB)
		doit = TRUE;
	    else
		++p;
	    todo -= tab_pad;
	    ++ind_len;
	    ind_col += tab_pad;
	}
#else
	while (todo >= (int)curbuf->b_p_ts)
	{
	    if (*p != TAB)
		doit = TRUE;
	    else
		++p;
	    todo -= (int)curbuf->b_p_ts;
	    ++ind_len;
	    /* ind_done += (int)curbuf->b_p_ts; */
	}
#endif
    }
    /* count spaces required for indent */
    while (todo > 0)
    {
	if (*p != ' ')
	    doit = TRUE;
	else
	    ++p;
	--todo;
	++ind_len;
	/* ++ind_done; */
    }

    /* Return if the indent is OK already. */
    if (!doit && !VIM_ISWHITE(*p) && !(flags & SIN_INSERT))
	return FALSE;

    /* Allocate memory for the new line. */
    if (flags & SIN_INSERT)
	p = oldline;
    else
	p = skipwhite(p);
    line_len = (int)STRLEN(p) + 1;

    /* If 'preserveindent' and 'expandtab' are both set keep the original
     * characters and allocate accordingly.  We will fill the rest with spaces
     * after the if (!curbuf->b_p_et) below. */
    if (orig_char_len != -1)
    {
	newline = alloc(orig_char_len + size - ind_done + line_len);
	if (newline == NULL)
	    return FALSE;
	todo = size - ind_done;
	ind_len = orig_char_len + todo;    /* Set total length of indent in
					    * characters, which may have been
					    * undercounted until now  */
	p = oldline;
	s = newline;
	while (orig_char_len > 0)
	{
	    *s++ = *p++;
	    orig_char_len--;
	}

	/* Skip over any additional white space (useful when newindent is less
	 * than old) */
	while (VIM_ISWHITE(*p))
	    ++p;

    }
    else
    {
	todo = size;
	newline = alloc(ind_len + line_len);
	if (newline == NULL)
	    return FALSE;
	s = newline;
    }

    /* Put the characters in the new line. */
    /* if 'expandtab' isn't set: use TABs */
    if (!curbuf->b_p_et)
    {
	/* If 'preserveindent' is set then reuse as much as possible of
	 * the existing indent structure for the new indent */
	if (!(flags & SIN_INSERT) && curbuf->b_p_pi)
	{
	    p = oldline;
	    ind_done = 0;

	    while (todo > 0 && VIM_ISWHITE(*p))
	    {
		if (*p == TAB)
		{
#ifdef FEAT_VARTABS
		    tab_pad = tabstop_padding(ind_done, curbuf->b_p_ts,
							curbuf->b_p_vts_array);
#else
		    tab_pad = (int)curbuf->b_p_ts
					   - (ind_done % (int)curbuf->b_p_ts);
#endif
		    /* stop if this tab will overshoot the target */
		    if (todo < tab_pad)
			break;
		    todo -= tab_pad;
		    ind_done += tab_pad;
		}
		else
		{
		    --todo;
		    ++ind_done;
		}
		*s++ = *p++;
	    }

	    /* Fill to next tabstop with a tab, if possible */
#ifdef FEAT_VARTABS
	    tab_pad = tabstop_padding(ind_done, curbuf->b_p_ts,
						curbuf->b_p_vts_array);
#else
	    tab_pad = (int)curbuf->b_p_ts - (ind_done % (int)curbuf->b_p_ts);
#endif
	    if (todo >= tab_pad)
	    {
		*s++ = TAB;
		todo -= tab_pad;
#ifdef FEAT_VARTABS
		ind_done += tab_pad;
#endif
	    }

	    p = skipwhite(p);
	}

#ifdef FEAT_VARTABS
	for (;;)
	{
	    tab_pad = tabstop_padding(ind_done, curbuf->b_p_ts,
							curbuf->b_p_vts_array);
	    if (todo < tab_pad)
		break;
	    *s++ = TAB;
	    todo -= tab_pad;
	    ind_done += tab_pad;
	}
#else
	while (todo >= (int)curbuf->b_p_ts)
	{
	    *s++ = TAB;
	    todo -= (int)curbuf->b_p_ts;
	}
#endif
    }
    while (todo > 0)
    {
	*s++ = ' ';
	--todo;
    }
    mch_memmove(s, p, (size_t)line_len);

    // Replace the line (unless undo fails).
    if (!(flags & SIN_UNDO) || u_savesub(curwin->w_cursor.lnum) == OK)
    {
	ml_replace(curwin->w_cursor.lnum, newline, FALSE);
	if (flags & SIN_CHANGED)
	    changed_bytes(curwin->w_cursor.lnum, 0);

	// Correct saved cursor position if it is in this line.
	if (saved_cursor.lnum == curwin->w_cursor.lnum)
	{
	    if (saved_cursor.col >= (colnr_T)(p - oldline))
		// cursor was after the indent, adjust for the number of
		// bytes added/removed
		saved_cursor.col += ind_len - (colnr_T)(p - oldline);
	    else if (saved_cursor.col >= (colnr_T)(s - newline))
		// cursor was in the indent, and is now after it, put it back
		// at the start of the indent (replacing spaces with TAB)
		saved_cursor.col = (colnr_T)(s - newline);
	}
#ifdef FEAT_TEXT_PROP
	{
	    int added = ind_len - (colnr_T)(p - oldline);

	    // When increasing indent this behaves like spaces were inserted at
	    // the old indent, when decreasing indent it behaves like spaces
	    // were deleted at the new indent.
	    adjust_prop_columns(curwin->w_cursor.lnum,
		 (colnr_T)(added > 0 ? (p - oldline) : ind_len), added, 0);
	}
#endif
	retval = TRUE;
    }
    else
	vim_free(newline);

    curwin->w_cursor.col = ind_len;
    return retval;
}

/*
 * Return the indent of the current line after a number.  Return -1 if no
 * number was found.  Used for 'n' in 'formatoptions': numbered list.
 * Since a pattern is used it can actually handle more than numbers.
 */
    int
get_number_indent(linenr_T lnum)
{
    colnr_T	col;
    pos_T	pos;

    regmatch_T	regmatch;
    int		lead_len = 0;	/* length of comment leader */

    if (lnum > curbuf->b_ml.ml_line_count)
	return -1;
    pos.lnum = 0;

#ifdef FEAT_COMMENTS
    /* In format_lines() (i.e. not insert mode), fo+=q is needed too...  */
    if ((State & INSERT) || has_format_option(FO_Q_COMS))
	lead_len = get_leader_len(ml_get(lnum), NULL, FALSE, TRUE);
#endif
    regmatch.regprog = vim_regcomp(curbuf->b_p_flp, RE_MAGIC);
    if (regmatch.regprog != NULL)
    {
	regmatch.rm_ic = FALSE;

	/* vim_regexec() expects a pointer to a line.  This lets us
	 * start matching for the flp beyond any comment leader...  */
	if (vim_regexec(&regmatch, ml_get(lnum) + lead_len, (colnr_T)0))
	{
	    pos.lnum = lnum;
	    pos.col = (colnr_T)(*regmatch.endp - ml_get(lnum));
	    pos.coladd = 0;
	}
	vim_regfree(regmatch.regprog);
    }

    if (pos.lnum == 0 || *ml_get_pos(&pos) == NUL)
	return -1;
    getvcol(curwin, &pos, &col, NULL, NULL);
    return (int)col;
}

#if defined(FEAT_LINEBREAK) || defined(PROTO)
/*
 * Return appropriate space number for breakindent, taking influencing
 * parameters into account. Window must be specified, since it is not
 * necessarily always the current one.
 */
    int
get_breakindent_win(
    win_T	*wp,
    char_u	*line) /* start of the line */
{
    static int	    prev_indent = 0;  /* cached indent value */
    static long	    prev_ts     = 0L; /* cached tabstop value */
    static char_u   *prev_line = NULL; /* cached pointer to line */
    static varnumber_T prev_tick = 0;   /* changedtick of cached value */
#ifdef FEAT_VARTABS
    static int      *prev_vts = NULL;    /* cached vartabs values */
#endif
    int		    bri = 0;
    /* window width minus window margin space, i.e. what rests for text */
    const int	    eff_wwidth = wp->w_width
			    - ((wp->w_p_nu || wp->w_p_rnu)
				&& (vim_strchr(p_cpo, CPO_NUMCOL) == NULL)
						? number_width(wp) + 1 : 0);

    /* used cached indent, unless pointer or 'tabstop' changed */
    if (prev_line != line || prev_ts != wp->w_buffer->b_p_ts
	    || prev_tick != CHANGEDTICK(wp->w_buffer)
#ifdef FEAT_VARTABS
	    || prev_vts != wp->w_buffer->b_p_vts_array
#endif
	)
    {
	prev_line = line;
	prev_ts = wp->w_buffer->b_p_ts;
	prev_tick = CHANGEDTICK(wp->w_buffer);
#ifdef FEAT_VARTABS
	prev_vts = wp->w_buffer->b_p_vts_array;
	prev_indent = get_indent_str_vtab(line,
				     (int)wp->w_buffer->b_p_ts,
				    wp->w_buffer->b_p_vts_array, wp->w_p_list);
#else
	prev_indent = get_indent_str(line,
				     (int)wp->w_buffer->b_p_ts, wp->w_p_list);
#endif
    }
    bri = prev_indent + wp->w_p_brishift;

    /* indent minus the length of the showbreak string */
    if (wp->w_p_brisbr)
	bri -= vim_strsize(p_sbr);

    /* Add offset for number column, if 'n' is in 'cpoptions' */
    bri += win_col_off2(wp);

    /* never indent past left window margin */
    if (bri < 0)
	bri = 0;
    /* always leave at least bri_min characters on the left,
     * if text width is sufficient */
    else if (bri > eff_wwidth - wp->w_p_brimin)
	bri = (eff_wwidth - wp->w_p_brimin < 0)
			    ? 0 : eff_wwidth - wp->w_p_brimin;

    return bri;
}
#endif

#if defined(FEAT_COMMENTS) || defined(PROTO)
/*
 * get_leader_len() returns the length in bytes of the prefix of the given
 * string which introduces a comment.  If this string is not a comment then
 * 0 is returned.
 * When "flags" is not NULL, it is set to point to the flags of the recognized
 * comment leader.
 * "backward" must be true for the "O" command.
 * If "include_space" is set, include trailing whitespace while calculating the
 * length.
 */
    int
get_leader_len(
    char_u	*line,
    char_u	**flags,
    int		backward,
    int		include_space)
{
    int		i, j;
    int		result;
    int		got_com = FALSE;
    int		found_one;
    char_u	part_buf[COM_MAX_LEN];	/* buffer for one option part */
    char_u	*string;		/* pointer to comment string */
    char_u	*list;
    int		middle_match_len = 0;
    char_u	*prev_list;
    char_u	*saved_flags = NULL;

    result = i = 0;
    while (VIM_ISWHITE(line[i]))    /* leading white space is ignored */
	++i;

    /*
     * Repeat to match several nested comment strings.
     */
    while (line[i] != NUL)
    {
	/*
	 * scan through the 'comments' option for a match
	 */
	found_one = FALSE;
	for (list = curbuf->b_p_com; *list; )
	{
	    /* Get one option part into part_buf[].  Advance "list" to next
	     * one.  Put "string" at start of string.  */
	    if (!got_com && flags != NULL)
		*flags = list;	    /* remember where flags started */
	    prev_list = list;
	    (void)copy_option_part(&list, part_buf, COM_MAX_LEN, ",");
	    string = vim_strchr(part_buf, ':');
	    if (string == NULL)	    /* missing ':', ignore this part */
		continue;
	    *string++ = NUL;	    /* isolate flags from string */

	    /* If we found a middle match previously, use that match when this
	     * is not a middle or end. */
	    if (middle_match_len != 0
		    && vim_strchr(part_buf, COM_MIDDLE) == NULL
		    && vim_strchr(part_buf, COM_END) == NULL)
		break;

	    /* When we already found a nested comment, only accept further
	     * nested comments. */
	    if (got_com && vim_strchr(part_buf, COM_NEST) == NULL)
		continue;

	    /* When 'O' flag present and using "O" command skip this one. */
	    if (backward && vim_strchr(part_buf, COM_NOBACK) != NULL)
		continue;

	    /* Line contents and string must match.
	     * When string starts with white space, must have some white space
	     * (but the amount does not need to match, there might be a mix of
	     * TABs and spaces). */
	    if (VIM_ISWHITE(string[0]))
	    {
		if (i == 0 || !VIM_ISWHITE(line[i - 1]))
		    continue;  /* missing white space */
		while (VIM_ISWHITE(string[0]))
		    ++string;
	    }
	    for (j = 0; string[j] != NUL && string[j] == line[i + j]; ++j)
		;
	    if (string[j] != NUL)
		continue;  /* string doesn't match */

	    /* When 'b' flag used, there must be white space or an
	     * end-of-line after the string in the line. */
	    if (vim_strchr(part_buf, COM_BLANK) != NULL
			   && !VIM_ISWHITE(line[i + j]) && line[i + j] != NUL)
		continue;

	    /* We have found a match, stop searching unless this is a middle
	     * comment. The middle comment can be a substring of the end
	     * comment in which case it's better to return the length of the
	     * end comment and its flags.  Thus we keep searching with middle
	     * and end matches and use an end match if it matches better. */
	    if (vim_strchr(part_buf, COM_MIDDLE) != NULL)
	    {
		if (middle_match_len == 0)
		{
		    middle_match_len = j;
		    saved_flags = prev_list;
		}
		continue;
	    }
	    if (middle_match_len != 0 && j > middle_match_len)
		/* Use this match instead of the middle match, since it's a
		 * longer thus better match. */
		middle_match_len = 0;

	    if (middle_match_len == 0)
		i += j;
	    found_one = TRUE;
	    break;
	}

	if (middle_match_len != 0)
	{
	    /* Use the previously found middle match after failing to find a
	     * match with an end. */
	    if (!got_com && flags != NULL)
		*flags = saved_flags;
	    i += middle_match_len;
	    found_one = TRUE;
	}

	/* No match found, stop scanning. */
	if (!found_one)
	    break;

	result = i;

	/* Include any trailing white space. */
	while (VIM_ISWHITE(line[i]))
	    ++i;

	if (include_space)
	    result = i;

	/* If this comment doesn't nest, stop here. */
	got_com = TRUE;
	if (vim_strchr(part_buf, COM_NEST) == NULL)
	    break;
    }
    return result;
}

/*
 * Return the offset at which the last comment in line starts. If there is no
 * comment in the whole line, -1 is returned.
 *
 * When "flags" is not null, it is set to point to the flags describing the
 * recognized comment leader.
 */
    int
get_last_leader_offset(char_u *line, char_u **flags)
{
    int		result = -1;
    int		i, j;
    int		lower_check_bound = 0;
    char_u	*string;
    char_u	*com_leader;
    char_u	*com_flags;
    char_u	*list;
    int		found_one;
    char_u	part_buf[COM_MAX_LEN];	/* buffer for one option part */

    /*
     * Repeat to match several nested comment strings.
     */
    i = (int)STRLEN(line);
    while (--i >= lower_check_bound)
    {
	/*
	 * scan through the 'comments' option for a match
	 */
	found_one = FALSE;
	for (list = curbuf->b_p_com; *list; )
	{
	    char_u *flags_save = list;

	    /*
	     * Get one option part into part_buf[].  Advance list to next one.
	     * put string at start of string.
	     */
	    (void)copy_option_part(&list, part_buf, COM_MAX_LEN, ",");
	    string = vim_strchr(part_buf, ':');
	    if (string == NULL)	/* If everything is fine, this cannot actually
				 * happen. */
		continue;
	    *string++ = NUL;	/* Isolate flags from string. */
	    com_leader = string;

	    /*
	     * Line contents and string must match.
	     * When string starts with white space, must have some white space
	     * (but the amount does not need to match, there might be a mix of
	     * TABs and spaces).
	     */
	    if (VIM_ISWHITE(string[0]))
	    {
		if (i == 0 || !VIM_ISWHITE(line[i - 1]))
		    continue;
		while (VIM_ISWHITE(*string))
		    ++string;
	    }
	    for (j = 0; string[j] != NUL && string[j] == line[i + j]; ++j)
		/* do nothing */;
	    if (string[j] != NUL)
		continue;

	    /*
	     * When 'b' flag used, there must be white space or an
	     * end-of-line after the string in the line.
	     */
	    if (vim_strchr(part_buf, COM_BLANK) != NULL
		    && !VIM_ISWHITE(line[i + j]) && line[i + j] != NUL)
		continue;

	    if (vim_strchr(part_buf, COM_MIDDLE) != NULL)
	    {
		// For a middlepart comment, only consider it to match if
		// everything before the current position in the line is
		// whitespace.  Otherwise we would think we are inside a
		// comment if the middle part appears somewhere in the middle
		// of the line.  E.g. for C the "*" appears often.
		for (j = 0; VIM_ISWHITE(line[j]) && j <= i; j++)
		    ;
		if (j < i)
		    continue;
	    }

	    /*
	     * We have found a match, stop searching.
	     */
	    found_one = TRUE;

	    if (flags)
		*flags = flags_save;
	    com_flags = flags_save;

	    break;
	}

	if (found_one)
	{
	    char_u  part_buf2[COM_MAX_LEN];	/* buffer for one option part */
	    int     len1, len2, off;

	    result = i;
	    /*
	     * If this comment nests, continue searching.
	     */
	    if (vim_strchr(part_buf, COM_NEST) != NULL)
		continue;

	    lower_check_bound = i;

	    /* Let's verify whether the comment leader found is a substring
	     * of other comment leaders. If it is, let's adjust the
	     * lower_check_bound so that we make sure that we have determined
	     * the comment leader correctly.
	     */

	    while (VIM_ISWHITE(*com_leader))
		++com_leader;
	    len1 = (int)STRLEN(com_leader);

	    for (list = curbuf->b_p_com; *list; )
	    {
		char_u *flags_save = list;

		(void)copy_option_part(&list, part_buf2, COM_MAX_LEN, ",");
		if (flags_save == com_flags)
		    continue;
		string = vim_strchr(part_buf2, ':');
		++string;
		while (VIM_ISWHITE(*string))
		    ++string;
		len2 = (int)STRLEN(string);
		if (len2 == 0)
		    continue;

		/* Now we have to verify whether string ends with a substring
		 * beginning the com_leader. */
		for (off = (len2 > i ? i : len2); off > 0 && off + len1 > len2;)
		{
		    --off;
		    if (!STRNCMP(string + off, com_leader, len2 - off))
		    {
			if (i - off < lower_check_bound)
			    lower_check_bound = i - off;
		    }
		}
	    }
	}
    }
    return result;
}
#endif

/*
 * Return the number of window lines occupied by buffer line "lnum".
 */
    int
plines(linenr_T lnum)
{
    return plines_win(curwin, lnum, TRUE);
}

    int
plines_win(
    win_T	*wp,
    linenr_T	lnum,
    int		winheight)	/* when TRUE limit to window height */
{
#if defined(FEAT_DIFF) || defined(PROTO)
    /* Check for filler lines above this buffer line.  When folded the result
     * is one line anyway. */
    return plines_win_nofill(wp, lnum, winheight) + diff_check_fill(wp, lnum);
}

    int
plines_nofill(linenr_T lnum)
{
    return plines_win_nofill(curwin, lnum, TRUE);
}

    int
plines_win_nofill(
    win_T	*wp,
    linenr_T	lnum,
    int		winheight)	/* when TRUE limit to window height */
{
#endif
    int		lines;

    if (!wp->w_p_wrap)
	return 1;

    if (wp->w_width == 0)
	return 1;

#ifdef FEAT_FOLDING
    /* A folded lines is handled just like an empty line. */
    /* NOTE: Caller must handle lines that are MAYBE folded. */
    if (lineFolded(wp, lnum) == TRUE)
	return 1;
#endif

    lines = plines_win_nofold(wp, lnum);
    if (winheight > 0 && lines > wp->w_height)
	return (int)wp->w_height;
    return lines;
}

/*
 * Return number of window lines physical line "lnum" will occupy in window
 * "wp".  Does not care about folding, 'wrap' or 'diff'.
 */
    int
plines_win_nofold(win_T *wp, linenr_T lnum)
{
    char_u	*s;
    long	col;
    int		width;

    s = ml_get_buf(wp->w_buffer, lnum, FALSE);
    if (*s == NUL)		/* empty line */
	return 1;
    col = win_linetabsize(wp, s, (colnr_T)MAXCOL);

    /*
     * If list mode is on, then the '$' at the end of the line may take up one
     * extra column.
     */
    if (wp->w_p_list && lcs_eol != NUL)
	col += 1;

    /*
     * Add column offset for 'number', 'relativenumber' and 'foldcolumn'.
     */
    width = wp->w_width - win_col_off(wp);
    if (width <= 0)
	return 32000;
    if (col <= width)
	return 1;
    col -= width;
    width += win_col_off2(wp);
    return (col + (width - 1)) / width + 1;
}

/*
 * Like plines_win(), but only reports the number of physical screen lines
 * used from the start of the line to the given column number.
 */
    int
plines_win_col(win_T *wp, linenr_T lnum, long column)
{
    long	col;
    char_u	*s;
    int		lines = 0;
    int		width;
    char_u	*line;

#ifdef FEAT_DIFF
    /* Check for filler lines above this buffer line.  When folded the result
     * is one line anyway. */
    lines = diff_check_fill(wp, lnum);
#endif

    if (!wp->w_p_wrap)
	return lines + 1;

    if (wp->w_width == 0)
	return lines + 1;

    line = s = ml_get_buf(wp->w_buffer, lnum, FALSE);

    col = 0;
    while (*s != NUL && --column >= 0)
    {
	col += win_lbr_chartabsize(wp, line, s, (colnr_T)col, NULL);
	MB_PTR_ADV(s);
    }

    /*
     * If *s is a TAB, and the TAB is not displayed as ^I, and we're not in
     * INSERT mode, then col must be adjusted so that it represents the last
     * screen position of the TAB.  This only fixes an error when the TAB wraps
     * from one screen line to the next (when 'columns' is not a multiple of
     * 'ts') -- webb.
     */
    if (*s == TAB && (State & NORMAL) && (!wp->w_p_list || lcs_tab1))
	col += win_lbr_chartabsize(wp, line, s, (colnr_T)col, NULL) - 1;

    /*
     * Add column offset for 'number', 'relativenumber', 'foldcolumn', etc.
     */
    width = wp->w_width - win_col_off(wp);
    if (width <= 0)
	return 9999;

    lines += 1;
    if (col > width)
	lines += (col - width) / (width + win_col_off2(wp)) + 1;
    return lines;
}

    int
plines_m_win(win_T *wp, linenr_T first, linenr_T last)
{
    int		count = 0;

    while (first <= last)
    {
#ifdef FEAT_FOLDING
	int	x;

	/* Check if there are any really folded lines, but also included lines
	 * that are maybe folded. */
	x = foldedCount(wp, first, NULL);
	if (x > 0)
	{
	    ++count;	    /* count 1 for "+-- folded" line */
	    first += x;
	}
	else
#endif
	{
#ifdef FEAT_DIFF
	    if (first == wp->w_topline)
		count += plines_win_nofill(wp, first, TRUE) + wp->w_topfill;
	    else
#endif
		count += plines_win(wp, first, TRUE);
	    ++first;
	}
    }
    return (count);
}

    int
gchar_pos(pos_T *pos)
{
    char_u	*ptr;

    /* When searching columns is sometimes put at the end of a line. */
    if (pos->col == MAXCOL)
	return NUL;
    ptr = ml_get_pos(pos);
    if (has_mbyte)
	return (*mb_ptr2char)(ptr);
    return (int)*ptr;
}

    int
gchar_cursor(void)
{
    if (has_mbyte)
	return (*mb_ptr2char)(ml_get_cursor());
    return (int)*ml_get_cursor();
}

/*
 * Write a character at the current cursor position.
 * It is directly written into the block.
 */
    void
pchar_cursor(int c)
{
    *(ml_get_buf(curbuf, curwin->w_cursor.lnum, TRUE)
						  + curwin->w_cursor.col) = c;
}

/*
 * When extra == 0: Return TRUE if the cursor is before or on the first
 *		    non-blank in the line.
 * When extra == 1: Return TRUE if the cursor is before the first non-blank in
 *		    the line.
 */
    int
inindent(int extra)
{
    char_u	*ptr;
    colnr_T	col;

    for (col = 0, ptr = ml_get_curline(); VIM_ISWHITE(*ptr); ++col)
	++ptr;
    if (col >= curwin->w_cursor.col + extra)
	return TRUE;
    else
	return FALSE;
}

/*
 * Skip to next part of an option argument: Skip space and comma.
 */
    char_u *
skip_to_option_part(char_u *p)
{
    if (*p == ',')
	++p;
    while (*p == ' ')
	++p;
    return p;
}

/*
 * check_status: called when the status bars for the buffer 'buf'
 *		 need to be updated
 */
    void
check_status(buf_T *buf)
{
    win_T	*wp;

    FOR_ALL_WINDOWS(wp)
	if (wp->w_buffer == buf && wp->w_status_height)
	{
	    wp->w_redr_status = TRUE;
	    if (must_redraw < VALID)
		must_redraw = VALID;
	}
}

/*
 * Ask for a reply from the user, a 'y' or a 'n'.
 * No other characters are accepted, the message is repeated until a valid
 * reply is entered or CTRL-C is hit.
 * If direct is TRUE, don't use vgetc() but ui_inchar(), don't get characters
 * from any buffers but directly from the user.
 *
 * return the 'y' or 'n'
 */
    int
ask_yesno(char_u *str, int direct)
{
    int	    r = ' ';
    int	    save_State = State;

    if (exiting)		/* put terminal in raw mode for this question */
	settmode(TMODE_RAW);
    ++no_wait_return;
#ifdef USE_ON_FLY_SCROLL
    dont_scroll = TRUE;		/* disallow scrolling here */
#endif
    State = CONFIRM;		/* mouse behaves like with :confirm */
#ifdef FEAT_MOUSE
    setmouse();			/* disables mouse for xterm */
#endif
    ++no_mapping;
    ++allow_keys;		/* no mapping here, but recognize keys */

    while (r != 'y' && r != 'n')
    {
	/* same highlighting as for wait_return */
	smsg_attr(HL_ATTR(HLF_R), "%s (y/n)?", str);
	if (direct)
	    r = get_keystroke();
	else
	    r = plain_vgetc();
	if (r == Ctrl_C || r == ESC)
	    r = 'n';
	msg_putchar(r);	    /* show what you typed */
	out_flush();
    }
    --no_wait_return;
    State = save_State;
#ifdef FEAT_MOUSE
    setmouse();
#endif
    --no_mapping;
    --allow_keys;

    return r;
}

#if defined(FEAT_MOUSE) || defined(PROTO)
/*
 * Return TRUE if "c" is a mouse key.
 */
    int
is_mouse_key(int c)
{
    return c == K_LEFTMOUSE
	|| c == K_LEFTMOUSE_NM
	|| c == K_LEFTDRAG
	|| c == K_LEFTRELEASE
	|| c == K_LEFTRELEASE_NM
	|| c == K_MOUSEMOVE
	|| c == K_MIDDLEMOUSE
	|| c == K_MIDDLEDRAG
	|| c == K_MIDDLERELEASE
	|| c == K_RIGHTMOUSE
	|| c == K_RIGHTDRAG
	|| c == K_RIGHTRELEASE
	|| c == K_MOUSEDOWN
	|| c == K_MOUSEUP
	|| c == K_MOUSELEFT
	|| c == K_MOUSERIGHT
	|| c == K_X1MOUSE
	|| c == K_X1DRAG
	|| c == K_X1RELEASE
	|| c == K_X2MOUSE
	|| c == K_X2DRAG
	|| c == K_X2RELEASE;
}
#endif

/*
 * Get a key stroke directly from the user.
 * Ignores mouse clicks and scrollbar events, except a click for the left
 * button (used at the more prompt).
 * Doesn't use vgetc(), because it syncs undo and eats mapped characters.
 * Disadvantage: typeahead is ignored.
 * Translates the interrupt character for unix to ESC.
 */
    int
get_keystroke(void)
{
    char_u	*buf = NULL;
    int		buflen = 150;
    int		maxlen;
    int		len = 0;
    int		n;
    int		save_mapped_ctrl_c = mapped_ctrl_c;
    int		waited = 0;

    mapped_ctrl_c = FALSE;	/* mappings are not used here */
    for (;;)
    {
	cursor_on();
	out_flush();

	/* Leave some room for check_termcode() to insert a key code into (max
	 * 5 chars plus NUL).  And fix_input_buffer() can triple the number of
	 * bytes. */
	maxlen = (buflen - 6 - len) / 3;
	if (buf == NULL)
	    buf = alloc(buflen);
	else if (maxlen < 10)
	{
	    char_u  *t_buf = buf;

	    /* Need some more space. This might happen when receiving a long
	     * escape sequence. */
	    buflen += 100;
	    buf = vim_realloc(buf, buflen);
	    if (buf == NULL)
		vim_free(t_buf);
	    maxlen = (buflen - 6 - len) / 3;
	}
	if (buf == NULL)
	{
	    do_outofmem_msg((long_u)buflen);
	    return ESC;  /* panic! */
	}

	/* First time: blocking wait.  Second time: wait up to 100ms for a
	 * terminal code to complete. */
	n = ui_inchar(buf + len, maxlen, len == 0 ? -1L : 100L, 0);
	if (n > 0)
	{
	    /* Replace zero and CSI by a special key code. */
	    n = fix_input_buffer(buf + len, n);
	    len += n;
	    waited = 0;
	}
	else if (len > 0)
	    ++waited;	    /* keep track of the waiting time */

	/* Incomplete termcode and not timed out yet: get more characters */
	if ((n = check_termcode(1, buf, buflen, &len)) < 0
	       && (!p_ttimeout || waited * 100L < (p_ttm < 0 ? p_tm : p_ttm)))
	    continue;

	if (n == KEYLEN_REMOVED)  /* key code removed */
	{
	    if (must_redraw != 0 && !need_wait_return && (State & CMDLINE) == 0)
	    {
		/* Redrawing was postponed, do it now. */
		update_screen(0);
		setcursor(); /* put cursor back where it belongs */
	    }
	    continue;
	}
	if (n > 0)		/* found a termcode: adjust length */
	    len = n;
	if (len == 0)		/* nothing typed yet */
	    continue;

	/* Handle modifier and/or special key code. */
	n = buf[0];
	if (n == K_SPECIAL)
	{
	    n = TO_SPECIAL(buf[1], buf[2]);
	    if (buf[1] == KS_MODIFIER
		    || n == K_IGNORE
#ifdef FEAT_MOUSE
		    || (is_mouse_key(n) && n != K_LEFTMOUSE)
#endif
#ifdef FEAT_GUI
		    || n == K_VER_SCROLLBAR
		    || n == K_HOR_SCROLLBAR
#endif
	       )
	    {
		if (buf[1] == KS_MODIFIER)
		    mod_mask = buf[2];
		len -= 3;
		if (len > 0)
		    mch_memmove(buf, buf + 3, (size_t)len);
		continue;
	    }
	    break;
	}
	if (has_mbyte)
	{
	    if (MB_BYTE2LEN(n) > len)
		continue;	/* more bytes to get */
	    buf[len >= buflen ? buflen - 1 : len] = NUL;
	    n = (*mb_ptr2char)(buf);
	}
#ifdef UNIX
	if (n == intr_char)
	    n = ESC;
#endif
	break;
    }
    vim_free(buf);

    mapped_ctrl_c = save_mapped_ctrl_c;
    return n;
}

/*
 * Get a number from the user.
 * When "mouse_used" is not NULL allow using the mouse.
 */
    int
get_number(
    int	    colon,			/* allow colon to abort */
    int	    *mouse_used)
{
    int	n = 0;
    int	c;
    int typed = 0;

    if (mouse_used != NULL)
	*mouse_used = FALSE;

    /* When not printing messages, the user won't know what to type, return a
     * zero (as if CR was hit). */
    if (msg_silent != 0)
	return 0;

#ifdef USE_ON_FLY_SCROLL
    dont_scroll = TRUE;		/* disallow scrolling here */
#endif
    ++no_mapping;
    ++allow_keys;		/* no mapping here, but recognize keys */
    for (;;)
    {
	windgoto(msg_row, msg_col);
	c = safe_vgetc();
	if (VIM_ISDIGIT(c))
	{
	    n = n * 10 + c - '0';
	    msg_putchar(c);
	    ++typed;
	}
	else if (c == K_DEL || c == K_KDEL || c == K_BS || c == Ctrl_H)
	{
	    if (typed > 0)
	    {
		msg_puts("\b \b");
		--typed;
	    }
	    n /= 10;
	}
#ifdef FEAT_MOUSE
	else if (mouse_used != NULL && c == K_LEFTMOUSE)
	{
	    *mouse_used = TRUE;
	    n = mouse_row + 1;
	    break;
	}
#endif
	else if (n == 0 && c == ':' && colon)
	{
	    stuffcharReadbuff(':');
	    if (!exmode_active)
		cmdline_row = msg_row;
	    skip_redraw = TRUE;	    /* skip redraw once */
	    do_redraw = FALSE;
	    break;
	}
	else if (c == CAR || c == NL || c == Ctrl_C || c == ESC)
	    break;
    }
    --no_mapping;
    --allow_keys;
    return n;
}

/*
 * Ask the user to enter a number.
 * When "mouse_used" is not NULL allow using the mouse and in that case return
 * the line number.
 */
    int
prompt_for_number(int *mouse_used)
{
    int		i;
    int		save_cmdline_row;
    int		save_State;

    /* When using ":silent" assume that <CR> was entered. */
    if (mouse_used != NULL)
	msg_puts(_("Type number and <Enter> or click with mouse (empty cancels): "));
    else
	msg_puts(_("Type number and <Enter> (empty cancels): "));

    // Set the state such that text can be selected/copied/pasted and we still
    // get mouse events. redraw_after_callback() will not redraw if cmdline_row
    // is zero.
    save_cmdline_row = cmdline_row;
    cmdline_row = 0;
    save_State = State;
    State = CMDLINE;
#ifdef FEAT_MOUSE
    // May show different mouse shape.
    setmouse();
#endif

    i = get_number(TRUE, mouse_used);
    if (KeyTyped)
    {
	// don't call wait_return() now
	if (msg_row > 0)
	    cmdline_row = msg_row - 1;
	need_wait_return = FALSE;
	msg_didany = FALSE;
	msg_didout = FALSE;
    }
    else
	cmdline_row = save_cmdline_row;
    State = save_State;
#ifdef FEAT_MOUSE
    // May need to restore mouse shape.
    setmouse();
#endif

    return i;
}

    void
msgmore(long n)
{
    long pn;

    if (global_busy	    /* no messages now, wait until global is finished */
	    || !messaging())  /* 'lazyredraw' set, don't do messages now */
	return;

    /* We don't want to overwrite another important message, but do overwrite
     * a previous "more lines" or "fewer lines" message, so that "5dd" and
     * then "put" reports the last action. */
    if (keep_msg != NULL && !keep_msg_more)
	return;

    if (n > 0)
	pn = n;
    else
	pn = -n;

    if (pn > p_report)
    {
	if (n > 0)
	    vim_snprintf(msg_buf, MSG_BUF_LEN,
		    NGETTEXT("%ld more line", "%ld more lines", pn), pn);
	else
	    vim_snprintf(msg_buf, MSG_BUF_LEN,
		    NGETTEXT("%ld line less", "%ld fewer lines", pn), pn);
	if (got_int)
	    vim_strcat((char_u *)msg_buf, (char_u *)_(" (Interrupted)"),
								  MSG_BUF_LEN);
	if (msg(msg_buf))
	{
	    set_keep_msg((char_u *)msg_buf, 0);
	    keep_msg_more = TRUE;
	}
    }
}

/*
 * flush map and typeahead buffers and give a warning for an error
 */
    void
beep_flush(void)
{
    if (emsg_silent == 0)
    {
	flush_buffers(FLUSH_MINIMAL);
	vim_beep(BO_ERROR);
    }
}

/*
 * Give a warning for an error.
 */
    void
vim_beep(
    unsigned val) /* one of the BO_ values, e.g., BO_OPER */
{
#ifdef FEAT_EVAL
    called_vim_beep = TRUE;
#endif

    if (emsg_silent == 0)
    {
	if (!((bo_flags & val) || (bo_flags & BO_ALL)))
	{
#ifdef ELAPSED_FUNC
	    static int		did_init = FALSE;
	    static elapsed_T	start_tv;

	    /* Only beep once per half a second, otherwise a sequence of beeps
	     * would freeze Vim. */
	    if (!did_init || ELAPSED_FUNC(start_tv) > 500)
	    {
		did_init = TRUE;
		ELAPSED_INIT(start_tv);
#endif
		if (p_vb
#ifdef FEAT_GUI
			/* While the GUI is starting up the termcap is set for
			 * the GUI but the output still goes to a terminal. */
			&& !(gui.in_use && gui.starting)
#endif
			)
		{
		    out_str_cf(T_VB);
#ifdef FEAT_VTP
		    /* No restore color information, refresh the screen. */
		    if (has_vtp_working() != 0
# ifdef FEAT_TERMGUICOLORS
			    && (p_tgc || (!p_tgc && t_colors >= 256))
# endif
			)
		    {
			redraw_later(CLEAR);
			update_screen(0);
			redrawcmd();
		    }
#endif
		}
		else
		    out_char(BELL);
#ifdef ELAPSED_FUNC
	    }
#endif
	}

	/* When 'debug' contains "beep" produce a message.  If we are sourcing
	 * a script or executing a function give the user a hint where the beep
	 * comes from. */
	if (vim_strchr(p_debug, 'e') != NULL)
	{
	    msg_source(HL_ATTR(HLF_W));
	    msg_attr(_("Beep!"), HL_ATTR(HLF_W));
	}
    }
}

/*
 * To get the "real" home directory:
 * - get value of $HOME
 * For Unix:
 *  - go to that directory
 *  - do mch_dirname() to get the real name of that directory.
 *  This also works with mounts and links.
 *  Don't do this for MS-DOS, it will change the "current dir" for a drive.
 * For Windows:
 *  This code is duplicated in init_homedir() in dosinst.c.  Keep in sync!
 */
    void
init_homedir(void)
{
    char_u  *var;

    /* In case we are called a second time (when 'encoding' changes). */
    VIM_CLEAR(homedir);

#ifdef VMS
    var = mch_getenv((char_u *)"SYS$LOGIN");
#else
    var = mch_getenv((char_u *)"HOME");
#endif

#ifdef MSWIN
    /*
     * Typically, $HOME is not defined on Windows, unless the user has
     * specifically defined it for Vim's sake.  However, on Windows NT
     * platforms, $HOMEDRIVE and $HOMEPATH are automatically defined for
     * each user.  Try constructing $HOME from these.
     */
    if (var == NULL || *var == NUL)
    {
	char_u *homedrive, *homepath;

	homedrive = mch_getenv((char_u *)"HOMEDRIVE");
	homepath = mch_getenv((char_u *)"HOMEPATH");
	if (homepath == NULL || *homepath == NUL)
	    homepath = (char_u *)"\\";
	if (homedrive != NULL
			   && STRLEN(homedrive) + STRLEN(homepath) < MAXPATHL)
	{
	    sprintf((char *)NameBuff, "%s%s", homedrive, homepath);
	    if (NameBuff[0] != NUL)
		var = NameBuff;
	}
    }

    if (var == NULL)
	var = mch_getenv((char_u *)"USERPROFILE");

    /*
     * Weird but true: $HOME may contain an indirect reference to another
     * variable, esp. "%USERPROFILE%".  Happens when $USERPROFILE isn't set
     * when $HOME is being set.
     */
    if (var != NULL && *var == '%')
    {
	char_u	*p;
	char_u	*exp;

	p = vim_strchr(var + 1, '%');
	if (p != NULL)
	{
	    vim_strncpy(NameBuff, var + 1, p - (var + 1));
	    exp = mch_getenv(NameBuff);
	    if (exp != NULL && *exp != NUL
					&& STRLEN(exp) + STRLEN(p) < MAXPATHL)
	    {
		vim_snprintf((char *)NameBuff, MAXPATHL, "%s%s", exp, p + 1);
		var = NameBuff;
	    }
	}
    }

    if (var != NULL && *var == NUL)	/* empty is same as not set */
	var = NULL;

    if (enc_utf8 && var != NULL)
    {
	int	len;
	char_u  *pp = NULL;

	/* Convert from active codepage to UTF-8.  Other conversions are
	 * not done, because they would fail for non-ASCII characters. */
	acp_to_enc(var, (int)STRLEN(var), &pp, &len);
	if (pp != NULL)
	{
	    homedir = pp;
	    return;
	}
    }

    /*
     * Default home dir is C:/
     * Best assumption we can make in such a situation.
     */
    if (var == NULL)
	var = (char_u *)"C:/";
#endif

    if (var != NULL)
    {
#ifdef UNIX
	/*
	 * Change to the directory and get the actual path.  This resolves
	 * links.  Don't do it when we can't return.
	 */
	if (mch_dirname(NameBuff, MAXPATHL) == OK
					  && mch_chdir((char *)NameBuff) == 0)
	{
	    if (!mch_chdir((char *)var) && mch_dirname(IObuff, IOSIZE) == OK)
		var = IObuff;
	    if (mch_chdir((char *)NameBuff) != 0)
		emsg(_(e_prev_dir));
	}
#endif
	homedir = vim_strsave(var);
    }
}

#if defined(EXITFREE) || defined(PROTO)
    void
free_homedir(void)
{
    vim_free(homedir);
}

    void
free_users(void)
{
    ga_clear_strings(&ga_users);
}
#endif

/*
 * Call expand_env() and store the result in an allocated string.
 * This is not very memory efficient, this expects the result to be freed
 * again soon.
 */
    char_u *
expand_env_save(char_u *src)
{
    return expand_env_save_opt(src, FALSE);
}

/*
 * Idem, but when "one" is TRUE handle the string as one file name, only
 * expand "~" at the start.
 */
    char_u *
expand_env_save_opt(char_u *src, int one)
{
    char_u	*p;

    p = alloc(MAXPATHL);
    if (p != NULL)
	expand_env_esc(src, p, MAXPATHL, FALSE, one, NULL);
    return p;
}

/*
 * Expand environment variable with path name.
 * "~/" is also expanded, using $HOME.	For Unix "~user/" is expanded.
 * Skips over "\ ", "\~" and "\$" (not for Win32 though).
 * If anything fails no expansion is done and dst equals src.
 */
    void
expand_env(
    char_u	*src,		/* input string e.g. "$HOME/vim.hlp" */
    char_u	*dst,		/* where to put the result */
    int		dstlen)		/* maximum length of the result */
{
    expand_env_esc(src, dst, dstlen, FALSE, FALSE, NULL);
}

    void
expand_env_esc(
    char_u	*srcp,		/* input string e.g. "$HOME/vim.hlp" */
    char_u	*dst,		/* where to put the result */
    int		dstlen,		/* maximum length of the result */
    int		esc,		/* escape spaces in expanded variables */
    int		one,		/* "srcp" is one file name */
    char_u	*startstr)	/* start again after this (can be NULL) */
{
    char_u	*src;
    char_u	*tail;
    int		c;
    char_u	*var;
    int		copy_char;
    int		mustfree;	/* var was allocated, need to free it later */
    int		at_start = TRUE; /* at start of a name */
    int		startstr_len = 0;

    if (startstr != NULL)
	startstr_len = (int)STRLEN(startstr);

    src = skipwhite(srcp);
    --dstlen;		    /* leave one char space for "\," */
    while (*src && dstlen > 0)
    {
#ifdef FEAT_EVAL
	/* Skip over `=expr`. */
	if (src[0] == '`' && src[1] == '=')
	{
	    size_t len;

	    var = src;
	    src += 2;
	    (void)skip_expr(&src);
	    if (*src == '`')
		++src;
	    len = src - var;
	    if (len > (size_t)dstlen)
		len = dstlen;
	    vim_strncpy(dst, var, len);
	    dst += len;
	    dstlen -= (int)len;
	    continue;
	}
#endif
	copy_char = TRUE;
	if ((*src == '$'
#ifdef VMS
		    && at_start
#endif
	   )
#if defined(MSWIN)
		|| *src == '%'
#endif
		|| (*src == '~' && at_start))
	{
	    mustfree = FALSE;

	    /*
	     * The variable name is copied into dst temporarily, because it may
	     * be a string in read-only memory and a NUL needs to be appended.
	     */
	    if (*src != '~')				/* environment var */
	    {
		tail = src + 1;
		var = dst;
		c = dstlen - 1;

#ifdef UNIX
		/* Unix has ${var-name} type environment vars */
		if (*tail == '{' && !vim_isIDc('{'))
		{
		    tail++;	/* ignore '{' */
		    while (c-- > 0 && *tail && *tail != '}')
			*var++ = *tail++;
		}
		else
#endif
		{
		    while (c-- > 0 && *tail != NUL && ((vim_isIDc(*tail))
#if defined(MSWIN)
			    || (*src == '%' && *tail != '%')
#endif
			    ))
			*var++ = *tail++;
		}

#if defined(MSWIN) || defined(UNIX)
# ifdef UNIX
		if (src[1] == '{' && *tail != '}')
# else
		if (*src == '%' && *tail != '%')
# endif
		    var = NULL;
		else
		{
# ifdef UNIX
		    if (src[1] == '{')
# else
		    if (*src == '%')
#endif
			++tail;
#endif
		    *var = NUL;
		    var = vim_getenv(dst, &mustfree);
#if defined(MSWIN) || defined(UNIX)
		}
#endif
	    }
							/* home directory */
	    else if (  src[1] == NUL
		    || vim_ispathsep(src[1])
		    || vim_strchr((char_u *)" ,\t\n", src[1]) != NULL)
	    {
		var = homedir;
		tail = src + 1;
	    }
	    else					/* user directory */
	    {
#if defined(UNIX) || (defined(VMS) && defined(USER_HOME))
		/*
		 * Copy ~user to dst[], so we can put a NUL after it.
		 */
		tail = src;
		var = dst;
		c = dstlen - 1;
		while (	   c-- > 0
			&& *tail
			&& vim_isfilec(*tail)
			&& !vim_ispathsep(*tail))
		    *var++ = *tail++;
		*var = NUL;
# ifdef UNIX
		/*
		 * If the system supports getpwnam(), use it.
		 * Otherwise, or if getpwnam() fails, the shell is used to
		 * expand ~user.  This is slower and may fail if the shell
		 * does not support ~user (old versions of /bin/sh).
		 */
#  if defined(HAVE_GETPWNAM) && defined(HAVE_PWD_H)
		{
		    /* Note: memory allocated by getpwnam() is never freed.
		     * Calling endpwent() apparently doesn't help. */
		    struct passwd *pw = (*dst == NUL)
					? NULL : getpwnam((char *)dst + 1);

		    var = (pw == NULL) ? NULL : (char_u *)pw->pw_dir;
		}
		if (var == NULL)
#  endif
		{
		    expand_T	xpc;

		    ExpandInit(&xpc);
		    xpc.xp_context = EXPAND_FILES;
		    var = ExpandOne(&xpc, dst, NULL,
				WILD_ADD_SLASH|WILD_SILENT, WILD_EXPAND_FREE);
		    mustfree = TRUE;
		}

# else	/* !UNIX, thus VMS */
		/*
		 * USER_HOME is a comma-separated list of
		 * directories to search for the user account in.
		 */
		{
		    char_u	test[MAXPATHL], paths[MAXPATHL];
		    char_u	*path, *next_path, *ptr;
		    stat_T	st;

		    STRCPY(paths, USER_HOME);
		    next_path = paths;
		    while (*next_path)
		    {
			for (path = next_path; *next_path && *next_path != ',';
				next_path++);
			if (*next_path)
			    *next_path++ = NUL;
			STRCPY(test, path);
			STRCAT(test, "/");
			STRCAT(test, dst + 1);
			if (mch_stat(test, &st) == 0)
			{
			    var = alloc(STRLEN(test) + 1);
			    STRCPY(var, test);
			    mustfree = TRUE;
			    break;
			}
		    }
		}
# endif /* UNIX */
#else
		/* cannot expand user's home directory, so don't try */
		var = NULL;
		tail = (char_u *)"";	/* for gcc */
#endif /* UNIX || VMS */
	    }

#ifdef BACKSLASH_IN_FILENAME
	    /* If 'shellslash' is set change backslashes to forward slashes.
	     * Can't use slash_adjust(), p_ssl may be set temporarily. */
	    if (p_ssl && var != NULL && vim_strchr(var, '\\') != NULL)
	    {
		char_u	*p = vim_strsave(var);

		if (p != NULL)
		{
		    if (mustfree)
			vim_free(var);
		    var = p;
		    mustfree = TRUE;
		    forward_slash(var);
		}
	    }
#endif

	    /* If "var" contains white space, escape it with a backslash.
	     * Required for ":e ~/tt" when $HOME includes a space. */
	    if (esc && var != NULL && vim_strpbrk(var, (char_u *)" \t") != NULL)
	    {
		char_u	*p = vim_strsave_escaped(var, (char_u *)" \t");

		if (p != NULL)
		{
		    if (mustfree)
			vim_free(var);
		    var = p;
		    mustfree = TRUE;
		}
	    }

	    if (var != NULL && *var != NUL
		    && (STRLEN(var) + STRLEN(tail) + 1 < (unsigned)dstlen))
	    {
		STRCPY(dst, var);
		dstlen -= (int)STRLEN(var);
		c = (int)STRLEN(var);
		/* if var[] ends in a path separator and tail[] starts
		 * with it, skip a character */
		if (*var != NUL && after_pathsep(dst, dst + c)
#if defined(BACKSLASH_IN_FILENAME) || defined(AMIGA)
			&& dst[-1] != ':'
#endif
			&& vim_ispathsep(*tail))
		    ++tail;
		dst += c;
		src = tail;
		copy_char = FALSE;
	    }
	    if (mustfree)
		vim_free(var);
	}

	if (copy_char)	    /* copy at least one char */
	{
	    /*
	     * Recognize the start of a new name, for '~'.
	     * Don't do this when "one" is TRUE, to avoid expanding "~" in
	     * ":edit foo ~ foo".
	     */
	    at_start = FALSE;
	    if (src[0] == '\\' && src[1] != NUL)
	    {
		*dst++ = *src++;
		--dstlen;
	    }
	    else if ((src[0] == ' ' || src[0] == ',') && !one)
		at_start = TRUE;
	    if (dstlen > 0)
	    {
		*dst++ = *src++;
		--dstlen;

		if (startstr != NULL && src - startstr_len >= srcp
			&& STRNCMP(src - startstr_len, startstr,
							    startstr_len) == 0)
		    at_start = TRUE;
	    }
	}

    }
    *dst = NUL;
}

/*
 * If the string between "p" and "pend" ends in "name/", return "pend" minus
 * the length of "name/".  Otherwise return "pend".
 */
    static char_u *
remove_tail(char_u *p, char_u *pend, char_u *name)
{
    int		len = (int)STRLEN(name) + 1;
    char_u	*newend = pend - len;

    if (newend >= p
	    && fnamencmp(newend, name, len - 1) == 0
	    && (newend == p || after_pathsep(p, newend)))
	return newend;
    return pend;
}

/*
 * Check if the directory "vimdir/<version>" or "vimdir/runtime" exists.
 * Return NULL if not, return its name in allocated memory otherwise.
 */
    static char_u *
vim_version_dir(char_u *vimdir)
{
    char_u	*p;

    if (vimdir == NULL || *vimdir == NUL)
	return NULL;
    p = concat_fnames(vimdir, (char_u *)VIM_VERSION_NODOT, TRUE);
    if (p != NULL && mch_isdir(p))
	return p;
    vim_free(p);
    p = concat_fnames(vimdir, (char_u *)RUNTIME_DIRNAME, TRUE);
    if (p != NULL && mch_isdir(p))
	return p;
    vim_free(p);
    return NULL;
}

/*
 * Vim's version of getenv().
 * Special handling of $HOME, $VIM and $VIMRUNTIME.
 * Also does ACP to 'enc' conversion for Win32.
 * "mustfree" is set to TRUE when returned is allocated, it must be
 * initialized to FALSE by the caller.
 */
    char_u *
vim_getenv(char_u *name, int *mustfree)
{
    char_u	*p = NULL;
    char_u	*pend;
    int		vimruntime;
#ifdef MSWIN
    WCHAR	*wn, *wp;

    // use "C:/" when $HOME is not set
    if (STRCMP(name, "HOME") == 0)
	return homedir;

    // Use Wide function
    wn = enc_to_utf16(name, NULL);
    if (wn == NULL)
	return NULL;

    wp = _wgetenv(wn);
    vim_free(wn);

    if (wp != NULL && *wp == NUL)   // empty is the same as not set
	wp = NULL;

    if (wp != NULL)
    {
	p = utf16_to_enc(wp, NULL);
	if (p == NULL)
	    return NULL;

	*mustfree = TRUE;
	return p;
    }
#else
    p = mch_getenv(name);
    if (p != NULL && *p == NUL)	    // empty is the same as not set
	p = NULL;

    if (p != NULL)
	return p;
#endif

    // handling $VIMRUNTIME and $VIM is below, bail out if it's another name.
    vimruntime = (STRCMP(name, "VIMRUNTIME") == 0);
    if (!vimruntime && STRCMP(name, "VIM") != 0)
	return NULL;

    /*
     * When expanding $VIMRUNTIME fails, try using $VIM/vim<version> or $VIM.
     * Don't do this when default_vimruntime_dir is non-empty.
     */
    if (vimruntime
#ifdef HAVE_PATHDEF
	    && *default_vimruntime_dir == NUL
#endif
       )
    {
#ifdef MSWIN
	// Use Wide function
	wp = _wgetenv(L"VIM");
	if (wp != NULL && *wp == NUL)	    // empty is the same as not set
	    wp = NULL;
	if (wp != NULL)
	{
	    char_u *q = utf16_to_enc(wp, NULL);
	    if (q != NULL)
	    {
		p = vim_version_dir(q);
		*mustfree = TRUE;
		if (p == NULL)
		    p = q;
	    }
	}
#else
	p = mch_getenv((char_u *)"VIM");
	if (p != NULL && *p == NUL)	    // empty is the same as not set
	    p = NULL;
	if (p != NULL)
	{
	    p = vim_version_dir(p);
	    if (p != NULL)
		*mustfree = TRUE;
	    else
		p = mch_getenv((char_u *)"VIM");
	}
#endif
    }

    /*
     * When expanding $VIM or $VIMRUNTIME fails, try using:
     * - the directory name from 'helpfile' (unless it contains '$')
     * - the executable name from argv[0]
     */
    if (p == NULL)
    {
	if (p_hf != NULL && vim_strchr(p_hf, '$') == NULL)
	    p = p_hf;
#ifdef USE_EXE_NAME
	/*
	 * Use the name of the executable, obtained from argv[0].
	 */
	else
	    p = exe_name;
#endif
	if (p != NULL)
	{
	    /* remove the file name */
	    pend = gettail(p);

	    /* remove "doc/" from 'helpfile', if present */
	    if (p == p_hf)
		pend = remove_tail(p, pend, (char_u *)"doc");

#ifdef USE_EXE_NAME
# ifdef MACOS_X
	    /* remove "MacOS" from exe_name and add "Resources/vim" */
	    if (p == exe_name)
	    {
		char_u	*pend1;
		char_u	*pnew;

		pend1 = remove_tail(p, pend, (char_u *)"MacOS");
		if (pend1 != pend)
		{
		    pnew = alloc(pend1 - p + 15);
		    if (pnew != NULL)
		    {
			STRNCPY(pnew, p, (pend1 - p));
			STRCPY(pnew + (pend1 - p), "Resources/vim");
			p = pnew;
			pend = p + STRLEN(p);
		    }
		}
	    }
# endif
	    /* remove "src/" from exe_name, if present */
	    if (p == exe_name)
		pend = remove_tail(p, pend, (char_u *)"src");
#endif

	    /* for $VIM, remove "runtime/" or "vim54/", if present */
	    if (!vimruntime)
	    {
		pend = remove_tail(p, pend, (char_u *)RUNTIME_DIRNAME);
		pend = remove_tail(p, pend, (char_u *)VIM_VERSION_NODOT);
	    }

	    /* remove trailing path separator */
	    if (pend > p && after_pathsep(p, pend))
		--pend;

#ifdef MACOS_X
	    if (p == exe_name || p == p_hf)
#endif
		/* check that the result is a directory name */
		p = vim_strnsave(p, (int)(pend - p));

	    if (p != NULL && !mch_isdir(p))
		VIM_CLEAR(p);
	    else
	    {
#ifdef USE_EXE_NAME
		/* may add "/vim54" or "/runtime" if it exists */
		if (vimruntime && (pend = vim_version_dir(p)) != NULL)
		{
		    vim_free(p);
		    p = pend;
		}
#endif
		*mustfree = TRUE;
	    }
	}
    }

#ifdef HAVE_PATHDEF
    /* When there is a pathdef.c file we can use default_vim_dir and
     * default_vimruntime_dir */
    if (p == NULL)
    {
	/* Only use default_vimruntime_dir when it is not empty */
	if (vimruntime && *default_vimruntime_dir != NUL)
	{
	    p = default_vimruntime_dir;
	    *mustfree = FALSE;
	}
	else if (*default_vim_dir != NUL)
	{
	    if (vimruntime && (p = vim_version_dir(default_vim_dir)) != NULL)
		*mustfree = TRUE;
	    else
	    {
		p = default_vim_dir;
		*mustfree = FALSE;
	    }
	}
    }
#endif

    /*
     * Set the environment variable, so that the new value can be found fast
     * next time, and others can also use it (e.g. Perl).
     */
    if (p != NULL)
    {
	if (vimruntime)
	{
	    vim_setenv((char_u *)"VIMRUNTIME", p);
	    didset_vimruntime = TRUE;
	}
	else
	{
	    vim_setenv((char_u *)"VIM", p);
	    didset_vim = TRUE;
	}
    }
    return p;
}

#if defined(FEAT_EVAL) || defined(PROTO)
    void
vim_unsetenv(char_u *var)
{
#ifdef HAVE_UNSETENV
    unsetenv((char *)var);
#else
    vim_setenv(var, (char_u *)"");
#endif
}
#endif


/*
 * Our portable version of setenv.
 */
    void
vim_setenv(char_u *name, char_u *val)
{
#ifdef HAVE_SETENV
    mch_setenv((char *)name, (char *)val, 1);
#else
    char_u	*envbuf;

    /*
     * Putenv does not copy the string, it has to remain
     * valid.  The allocated memory will never be freed.
     */
    envbuf = alloc(STRLEN(name) + STRLEN(val) + 2);
    if (envbuf != NULL)
    {
	sprintf((char *)envbuf, "%s=%s", name, val);
	putenv((char *)envbuf);
    }
#endif
#ifdef FEAT_GETTEXT
    /*
     * When setting $VIMRUNTIME adjust the directory to find message
     * translations to $VIMRUNTIME/lang.
     */
    if (*val != NUL && STRICMP(name, "VIMRUNTIME") == 0)
    {
	char_u	*buf = concat_str(val, (char_u *)"/lang");

	if (buf != NULL)
	{
	    bindtextdomain(VIMPACKAGE, (char *)buf);
	    vim_free(buf);
	}
    }
#endif
}

/*
 * Function given to ExpandGeneric() to obtain an environment variable name.
 */
    char_u *
get_env_name(
    expand_T	*xp UNUSED,
    int		idx)
{
# if defined(AMIGA)
    /*
     * No environ[] on the Amiga.
     */
    return NULL;
# else
# ifndef __WIN32__
    /* Borland C++ 5.2 has this in a header file. */
    extern char		**environ;
# endif
# define ENVNAMELEN 100
    static char_u	name[ENVNAMELEN];
    char_u		*str;
    int			n;

    str = (char_u *)environ[idx];
    if (str == NULL)
	return NULL;

    for (n = 0; n < ENVNAMELEN - 1; ++n)
    {
	if (str[n] == '=' || str[n] == NUL)
	    break;
	name[n] = str[n];
    }
    name[n] = NUL;
    return name;
# endif
}

/*
 * Add a user name to the list of users in ga_users.
 * Do nothing if user name is NULL or empty.
 */
    static void
add_user(char_u *user, int need_copy)
{
    char_u	*user_copy = (user != NULL && need_copy)
						    ? vim_strsave(user) : user;

    if (user_copy == NULL || *user_copy == NUL || ga_grow(&ga_users, 1) == FAIL)
    {
	if (need_copy)
	    vim_free(user);
	return;
    }
    ((char_u **)(ga_users.ga_data))[ga_users.ga_len++] = user_copy;
}

/*
 * Find all user names for user completion.
 * Done only once and then cached.
 */
    static void
init_users(void)
{
    static int	lazy_init_done = FALSE;

    if (lazy_init_done)
	return;

    lazy_init_done = TRUE;
    ga_init2(&ga_users, sizeof(char_u *), 20);

# if defined(HAVE_GETPWENT) && defined(HAVE_PWD_H)
    {
	struct passwd*	pw;

	setpwent();
	while ((pw = getpwent()) != NULL)
	    add_user((char_u *)pw->pw_name, TRUE);
	endpwent();
    }
# elif defined(MSWIN)
    {
	DWORD		nusers = 0, ntotal = 0, i;
	PUSER_INFO_0	uinfo;

	if (NetUserEnum(NULL, 0, 0, (LPBYTE *) &uinfo, MAX_PREFERRED_LENGTH,
				       &nusers, &ntotal, NULL) == NERR_Success)
	{
	    for (i = 0; i < nusers; i++)
		add_user(utf16_to_enc(uinfo[i].usri0_name, NULL), FALSE);

	    NetApiBufferFree(uinfo);
	}
    }
# endif
# if defined(HAVE_GETPWNAM)
    {
	char_u	*user_env = mch_getenv((char_u *)"USER");

	// The $USER environment variable may be a valid remote user name (NIS,
	// LDAP) not already listed by getpwent(), as getpwent() only lists
	// local user names.  If $USER is not already listed, check whether it
	// is a valid remote user name using getpwnam() and if it is, add it to
	// the list of user names.

	if (user_env != NULL && *user_env != NUL)
	{
	    int	i;

	    for (i = 0; i < ga_users.ga_len; i++)
	    {
		char_u	*local_user = ((char_u **)ga_users.ga_data)[i];

		if (STRCMP(local_user, user_env) == 0)
		    break;
	    }

	    if (i == ga_users.ga_len)
	    {
		struct passwd	*pw = getpwnam((char *)user_env);

		if (pw != NULL)
		    add_user((char_u *)pw->pw_name, TRUE);
	    }
	}
    }
# endif
}

/*
 * Function given to ExpandGeneric() to obtain an user names.
 */
    char_u*
get_users(expand_T *xp UNUSED, int idx)
{
    init_users();
    if (idx < ga_users.ga_len)
	return ((char_u **)ga_users.ga_data)[idx];
    return NULL;
}

/*
 * Check whether name matches a user name. Return:
 * 0 if name does not match any user name.
 * 1 if name partially matches the beginning of a user name.
 * 2 is name fully matches a user name.
 */
    int
match_user(char_u *name)
{
    int i;
    int n = (int)STRLEN(name);
    int result = 0;

    init_users();
    for (i = 0; i < ga_users.ga_len; i++)
    {
	if (STRCMP(((char_u **)ga_users.ga_data)[i], name) == 0)
	    return 2; /* full match */
	if (STRNCMP(((char_u **)ga_users.ga_data)[i], name, n) == 0)
	    result = 1; /* partial match */
    }
    return result;
}

/*
 * Concatenate two strings and return the result in allocated memory.
 * Returns NULL when out of memory.
 */
    char_u  *
concat_str(char_u *str1, char_u *str2)
{
    char_u  *dest;
    size_t  l = STRLEN(str1);

    dest = alloc(l + STRLEN(str2) + 1L);
    if (dest != NULL)
    {
	STRCPY(dest, str1);
	STRCPY(dest + l, str2);
    }
    return dest;
}

    static void
prepare_to_exit(void)
{
#if defined(SIGHUP) && defined(SIG_IGN)
    /* Ignore SIGHUP, because a dropped connection causes a read error, which
     * makes Vim exit and then handling SIGHUP causes various reentrance
     * problems. */
    signal(SIGHUP, SIG_IGN);
#endif

#ifdef FEAT_GUI
    if (gui.in_use)
    {
	gui.dying = TRUE;
	out_trash();	/* trash any pending output */
    }
    else
#endif
    {
	windgoto((int)Rows - 1, 0);

	/*
	 * Switch terminal mode back now, so messages end up on the "normal"
	 * screen (if there are two screens).
	 */
	settmode(TMODE_COOK);
	stoptermcap();
	out_flush();
    }
}

/*
 * Preserve files and exit.
 * When called IObuff must contain a message.
 * NOTE: This may be called from deathtrap() in a signal handler, avoid unsafe
 * functions, such as allocating memory.
 */
    void
preserve_exit(void)
{
    buf_T	*buf;

    prepare_to_exit();

    /* Setting this will prevent free() calls.  That avoids calling free()
     * recursively when free() was invoked with a bad pointer. */
    really_exiting = TRUE;

    out_str(IObuff);
    screen_start();		    /* don't know where cursor is now */
    out_flush();

    ml_close_notmod();		    /* close all not-modified buffers */

    FOR_ALL_BUFFERS(buf)
    {
	if (buf->b_ml.ml_mfp != NULL && buf->b_ml.ml_mfp->mf_fname != NULL)
	{
	    OUT_STR("Vim: preserving files...\n");
	    screen_start();	    /* don't know where cursor is now */
	    out_flush();
	    ml_sync_all(FALSE, FALSE);	/* preserve all swap files */
	    break;
	}
    }

    ml_close_all(FALSE);	    /* close all memfiles, without deleting */

    OUT_STR("Vim: Finished.\n");

    getout(1);
}

/*
 * Check for CTRL-C pressed, but only once in a while.
 * Should be used instead of ui_breakcheck() for functions that check for
 * each line in the file.  Calling ui_breakcheck() each time takes too much
 * time, because it can be a system call.
 */

#ifndef BREAKCHECK_SKIP
# define BREAKCHECK_SKIP 1000
#endif

static int	breakcheck_count = 0;

    void
line_breakcheck(void)
{
    if (++breakcheck_count >= BREAKCHECK_SKIP)
    {
	breakcheck_count = 0;
	ui_breakcheck();
    }
}

/*
 * Like line_breakcheck() but check 10 times less often.
 */
    void
fast_breakcheck(void)
{
    if (++breakcheck_count >= BREAKCHECK_SKIP * 10)
    {
	breakcheck_count = 0;
	ui_breakcheck();
    }
}

#if defined(VIM_BACKTICK) || defined(FEAT_EVAL) \
	|| (defined(HAVE_LOCALE_H) || defined(X_LOCALE)) \
	|| defined(PROTO)

#ifndef SEEK_SET
# define SEEK_SET 0
#endif
#ifndef SEEK_END
# define SEEK_END 2
#endif

/*
 * Get the stdout of an external command.
 * If "ret_len" is NULL replace NUL characters with NL.  When "ret_len" is not
 * NULL store the length there.
 * Returns an allocated string, or NULL for error.
 */
    char_u *
get_cmd_output(
    char_u	*cmd,
    char_u	*infile,	/* optional input file name */
    int		flags,		/* can be SHELL_SILENT */
    int		*ret_len)
{
    char_u	*tempname;
    char_u	*command;
    char_u	*buffer = NULL;
    int		len;
    int		i = 0;
    FILE	*fd;

    if (check_restricted() || check_secure())
	return NULL;

    /* get a name for the temp file */
    if ((tempname = vim_tempname('o', FALSE)) == NULL)
    {
	emsg(_(e_notmp));
	return NULL;
    }

    /* Add the redirection stuff */
    command = make_filter_cmd(cmd, infile, tempname);
    if (command == NULL)
	goto done;

    /*
     * Call the shell to execute the command (errors are ignored).
     * Don't check timestamps here.
     */
    ++no_check_timestamps;
    call_shell(command, SHELL_DOOUT | SHELL_EXPAND | flags);
    --no_check_timestamps;

    vim_free(command);

    /*
     * read the names from the file into memory
     */
# ifdef VMS
    /* created temporary file is not always readable as binary */
    fd = mch_fopen((char *)tempname, "r");
# else
    fd = mch_fopen((char *)tempname, READBIN);
# endif

    if (fd == NULL)
    {
	semsg(_(e_notopen), tempname);
	goto done;
    }

    fseek(fd, 0L, SEEK_END);
    len = ftell(fd);		    /* get size of temp file */
    fseek(fd, 0L, SEEK_SET);

    buffer = alloc(len + 1);
    if (buffer != NULL)
	i = (int)fread((char *)buffer, (size_t)1, (size_t)len, fd);
    fclose(fd);
    mch_remove(tempname);
    if (buffer == NULL)
	goto done;
#ifdef VMS
    len = i;	/* VMS doesn't give us what we asked for... */
#endif
    if (i != len)
    {
	semsg(_(e_notread), tempname);
	VIM_CLEAR(buffer);
    }
    else if (ret_len == NULL)
    {
	/* Change NUL into SOH, otherwise the string is truncated. */
	for (i = 0; i < len; ++i)
	    if (buffer[i] == NUL)
		buffer[i] = 1;

	buffer[len] = NUL;	/* make sure the buffer is terminated */
    }
    else
	*ret_len = len;

done:
    vim_free(tempname);
    return buffer;
}

# if defined(FEAT_EVAL) || defined(PROTO)

    static void
get_cmd_output_as_rettv(
    typval_T	*argvars,
    typval_T	*rettv,
    int		retlist)
{
    char_u	*res = NULL;
    char_u	*p;
    char_u	*infile = NULL;
    int		err = FALSE;
    FILE	*fd;
    list_T	*list = NULL;
    int		flags = SHELL_SILENT;

    rettv->v_type = VAR_STRING;
    rettv->vval.v_string = NULL;
    if (check_restricted() || check_secure())
	goto errret;

    if (argvars[1].v_type != VAR_UNKNOWN)
    {
	/*
	 * Write the text to a temp file, to be used for input of the shell
	 * command.
	 */
	if ((infile = vim_tempname('i', TRUE)) == NULL)
	{
	    emsg(_(e_notmp));
	    goto errret;
	}

	fd = mch_fopen((char *)infile, WRITEBIN);
	if (fd == NULL)
	{
	    semsg(_(e_notopen), infile);
	    goto errret;
	}
	if (argvars[1].v_type == VAR_NUMBER)
	{
	    linenr_T	lnum;
	    buf_T	*buf;

	    buf = buflist_findnr(argvars[1].vval.v_number);
	    if (buf == NULL)
	    {
		semsg(_(e_nobufnr), argvars[1].vval.v_number);
		fclose(fd);
		goto errret;
	    }

	    for (lnum = 1; lnum <= buf->b_ml.ml_line_count; lnum++)
	    {
		for (p = ml_get_buf(buf, lnum, FALSE); *p != NUL; ++p)
		    if (putc(*p == '\n' ? NUL : *p, fd) == EOF)
		    {
			err = TRUE;
			break;
		    }
		if (putc(NL, fd) == EOF)
		{
		    err = TRUE;
		    break;
		}
	    }
	}
	else if (argvars[1].v_type == VAR_LIST)
	{
	    if (write_list(fd, argvars[1].vval.v_list, TRUE) == FAIL)
		err = TRUE;
	}
	else
	{
	    size_t	len;
	    char_u	buf[NUMBUFLEN];

	    p = tv_get_string_buf_chk(&argvars[1], buf);
	    if (p == NULL)
	    {
		fclose(fd);
		goto errret;		/* type error; errmsg already given */
	    }
	    len = STRLEN(p);
	    if (len > 0 && fwrite(p, len, 1, fd) != 1)
		err = TRUE;
	}
	if (fclose(fd) != 0)
	    err = TRUE;
	if (err)
	{
	    emsg(_("E677: Error writing temp file"));
	    goto errret;
	}
    }

    /* Omit SHELL_COOKED when invoked with ":silent".  Avoids that the shell
     * echoes typeahead, that messes up the display. */
    if (!msg_silent)
	flags += SHELL_COOKED;

    if (retlist)
    {
	int		len;
	listitem_T	*li;
	char_u		*s = NULL;
	char_u		*start;
	char_u		*end;
	int		i;

	res = get_cmd_output(tv_get_string(&argvars[0]), infile, flags, &len);
	if (res == NULL)
	    goto errret;

	list = list_alloc();
	if (list == NULL)
	    goto errret;

	for (i = 0; i < len; ++i)
	{
	    start = res + i;
	    while (i < len && res[i] != NL)
		++i;
	    end = res + i;

	    s = alloc(end - start + 1);
	    if (s == NULL)
		goto errret;

	    for (p = s; start < end; ++p, ++start)
		*p = *start == NUL ? NL : *start;
	    *p = NUL;

	    li = listitem_alloc();
	    if (li == NULL)
	    {
		vim_free(s);
		goto errret;
	    }
	    li->li_tv.v_type = VAR_STRING;
	    li->li_tv.v_lock = 0;
	    li->li_tv.vval.v_string = s;
	    list_append(list, li);
	}

	rettv_list_set(rettv, list);
	list = NULL;
    }
    else
    {
	res = get_cmd_output(tv_get_string(&argvars[0]), infile, flags, NULL);
#ifdef USE_CRNL
	/* translate <CR><NL> into <NL> */
	if (res != NULL)
	{
	    char_u	*s, *d;

	    d = res;
	    for (s = res; *s; ++s)
	    {
		if (s[0] == CAR && s[1] == NL)
		    ++s;
		*d++ = *s;
	    }
	    *d = NUL;
	}
#endif
	rettv->vval.v_string = res;
	res = NULL;
    }

errret:
    if (infile != NULL)
    {
	mch_remove(infile);
	vim_free(infile);
    }
    if (res != NULL)
	vim_free(res);
    if (list != NULL)
	list_free(list);
}

/*
 * "system()" function
 */
    void
f_system(typval_T *argvars, typval_T *rettv)
{
    get_cmd_output_as_rettv(argvars, rettv, FALSE);
}

/*
 * "systemlist()" function
 */
    void
f_systemlist(typval_T *argvars, typval_T *rettv)
{
    get_cmd_output_as_rettv(argvars, rettv, TRUE);
}
# endif // FEAT_EVAL

#endif

/*
 * Return TRUE when need to go to Insert mode because of 'insertmode'.
 * Don't do this when still processing a command or a mapping.
 * Don't do this when inside a ":normal" command.
 */
    int
goto_im(void)
{
    return (p_im && stuff_empty() && typebuf_typed());
}

/*
 * Returns the isolated name of the shell in allocated memory:
 * - Skip beyond any path.  E.g., "/usr/bin/csh -f" -> "csh -f".
 * - Remove any argument.  E.g., "csh -f" -> "csh".
 * But don't allow a space in the path, so that this works:
 *   "/usr/bin/csh --rcfile ~/.cshrc"
 * But don't do that for Windows, it's common to have a space in the path.
 */
    char_u *
get_isolated_shell_name(void)
{
    char_u *p;

#ifdef MSWIN
    p = gettail(p_sh);
    p = vim_strnsave(p, (int)(skiptowhite(p) - p));
#else
    p = skiptowhite(p_sh);
    if (*p == NUL)
    {
	/* No white space, use the tail. */
	p = vim_strsave(gettail(p_sh));
    }
    else
    {
	char_u  *p1, *p2;

	/* Find the last path separator before the space. */
	p1 = p_sh;
	for (p2 = p_sh; p2 < p; MB_PTR_ADV(p2))
	    if (vim_ispathsep(*p2))
		p1 = p2 + 1;
	p = vim_strnsave(p1, (int)(p - p1));
    }
#endif
    return p;
}

/*
 * Check if the "://" of a URL is at the pointer, return URL_SLASH.
 * Also check for ":\\", which MS Internet Explorer accepts, return
 * URL_BACKSLASH.
 */
    int
path_is_url(char_u *p)
{
    if (STRNCMP(p, "://", (size_t)3) == 0)
	return URL_SLASH;
    else if (STRNCMP(p, ":\\\\", (size_t)3) == 0)
	return URL_BACKSLASH;
    return 0;
}

/*
 * Check if "fname" starts with "name://".  Return URL_SLASH if it does.
 * Return URL_BACKSLASH for "name:\\".
 * Return zero otherwise.
 */
    int
path_with_url(char_u *fname)
{
    char_u *p;

    for (p = fname; isalpha(*p); ++p)
	;
    return path_is_url(p);
}
