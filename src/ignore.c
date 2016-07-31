/************************************************************************
 * Copyright (C) 2005-2008 Philipp Marek.
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 ************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <pcre.h>
#include <sys/mman.h>


#include "global.h"
#include "waa.h"
#include "est_ops.h"
#include "helper.h"
#include "direnum.h"
#include "ignore.h"


/** \file
 * \ref ignore command and functions.
 * */

/* \note Due to restriction in C-comment syntax the above 
 * cases have to separate \c * and \c / to avoid breaking 
 * the code. \c * and \c / would belong together.
 * 
 * As a fix I wrote /§* and *§/, which get changed by a perl 
 * script after generation. */

/**
 * \addtogroup cmds
 * \section ignore
 *
 * \code 
 * fsvs ignore [prepend|append|at=n] pattern[s]
 * fsvs ignore dump|load
 * \endcode
 *
 * This command adds patterns to the end of the ignore list, 
 * or, with \e prepend , puts them at the beginning of the list.
 * With \c at=x the patterns are inserted at the position \c x , 
 * counting from 0.
 * 
 * <tt>fsvs dump</tt> prints the patterns to \c STDOUT . If there are
 * special characters like \c CR or \c LF embedded in the pattern 
 * <b>without encoding</b> (like \c \\r or \c \\n), the 
 * output will be garbled.
 * 
 * The patterns may include \c * and \c ? as wildcards in one directory 
 * level, or \c ** for arbitrary strings.
 *
 * These patterns are only matched against new files; entries that are 
 * already versioned are not invalidated.
 * If the given path matches a new directory, entries below aren't found, 
 * either; but if this directory or entries below are already versioned, 
 * the pattern doesn't work, as the match is restricted to the directory.
 *
 * So:
 * \code
 *     fsvs ignore ./tmp
 * \endcode
 * ignores the directory \c tmp; but if it has already been committed,
 * existing entries would have to be unmarked with \ref unversion 
 * "fsvs unversion".
 * Normally it's better to use
 * \code
 *     fsvs ignore ./tmp/§**
 * \endcode
 * as that takes the directory itself (which might be needed after restore
 * as a mount point), but ignore \b all entries below.
 * 
 *
 * Other special variants are available, see the documentation \ref ignpat .
 * 
 * Examples:
 * \code
 *     fsvs ignore ./proc
 *     fsvs ignore ./dev/pts
 *     fsvs ignore './var/log/§*-*'
 *     fsvs ignore './§**~'
 *     fsvs ignore './§**§/§*.bak'
 *     fsvs ignore prepend 't./§**.txt'
 *     fsvs ignore append 't./§**.svg'
 *     fsvs ignore at=1 './§**.tmp'
 *     fsvs ignore dump
 *     fsvs ignore dump -v
 *     echo "./§**.doc" | fsvs ignore load
 * \endcode
 *
 * \note Please take care that your wildcard patterns are not expanded
 * by the shell!
 * 
 **/

/**
 * \defgroup ignpat_dev Developers' reference
 * \ingroup ignpat
 * 
 * Internal structure, and some explanations.
 *
 * The ignore lists are first loaded into a global array.
 * Then they should be distributed onto the directory structure;
 * all applicable patterns get referenced by a directory.
 *
 * \todo Currently all patterns get tested against all new entries. This 
 * does not seem to be a performance problem.
 * 
 * Eg this directory tree
 * \code
 *     root
 *       +-- dirA
 *       +-- dirB
 *             +-- dirB1
 * \endcode
 * with these ignore patterns 
 * \code
 *        *.tmp
 *        **~
 *        dirA/tmp*.lst
 *        dirB/§**§/§*.o
 * \endcode
 * would result in
 * \code
 *     root:               *.tmp, **~
 *       +-- dirA          **~, tmp*.lst
 *       +-- dirB          **§/§*.o
 *             +-- dirB1   **§/§*.o
 * \endcode
 *
 * Ignore patterns apply only to \b new entries, ie. entries already
 * in the \c .waa-dir file get done as usual.
 * That's why we need an "add" command:
 * \code
 *     $ fsvs ignore '/proc/§*'
 *     $ fsvs add /proc/stat
 * \endcode
 * would version \c /proc/stat , but nothing else from \c /proc .
 * 
 * A negative ignore-list is named \e take list.
 * 
 * The default behaviour is best described with the take-list
 * \code
 *     add   = **
 * \endcode
 * 
 * The storage of these values will (sometime) be done in \c svn:ignore
 * and \c svn:recursive-ignore ; in the waa-area the format is
 * \code
 *     header: number of entries
 *     %u\n
 *     pattern\0\n
 *     pattern\0\n
 * \endcode
 * 
 * Whitespace are not allowed at the start of a pattern; use <c>./§*</c>
 * or something similar.
 * 
 * As low-level library pcre is used, the given shell-patterns are 
 * translated from the shell-like syntax into PCREs.
 * \code
 *     *		->	[^/]*
 *     **		->	.*
 *     ?		->	.
 *     .		->	\.
 * \endcode
 * All other \c \\W are escaped.
 * 
 **/


/** \defgroup ignpat Ignore patterns - user part
 * \ingroup add_unv_ign
 *
 * \section ignpat_why Why should I ignore files?
 *
 * Ignore patterns are used to ignore certain directory
 * entries, where versioning makes no sense to the user. If you're
 * versioning the complete installation of a machine, you wouldn't care to
 * store the contents of \c /proc (see <tt>man 5 proc</tt>), or possibly
 * because of security reasons you don't want \c /etc/shadow , \c
 * /etc/sshd/ssh_host_*key , and/or other password-containing files.
 * 
 * Ignore patterns allow you to define which directory entries (files,
 * subdirectories, devices, symlinks etc.) should be taken respectively
 * ignored.
 * 
 * There are some kinds of ignore patterns; they are listed below.
 * 
 * 
 * \section ign_shell Shell-like patterns
 * 
 * These must start with <tt>./</tt>, just like a base-directory-relative 
 * path.
 * \c ? , \c * as well as character classes \c [a-z] have their usual
 * meaning, and \c ** is a wildcard for directory levels.
 *
 * You can use a backslash \c \\ outside of character classes to match
 * usually special characters literally, eg. \c \\* within a pattern will
 * match a literal asterisk character within a file or directory name.
 * Within character classes all characters except \c ] are treated
 * literally. If a literal \c ] should be included in a character class,
 * it can be placed as the first character or also be escaped using a
 * backslash.
 *
 * Example for \c / as the base-directory
 * \code
 *     ./[oa]pt
 *     ./sys
 *     ./proc/§*
 *     ./home/§**~
 * \endcode
 *
 * This would ignore files and directories called \c apt or \c opt in the
 * root directory (and files below, in the case of a directory), the
 * directory \c /sys and everything below, the contents of \c /proc
 * (but take the directory itself, so that upon restore it gets created
 * as a mountpoint), and all entries matching \c *~ in and below
 * \c /home .
 * 
 *
 *
 * \note The patterns are anchored at the beginning and the end. So a 
 * pattern <tt>./sys</tt> will match \b only a file or directory named \c 
 * sys. If you want to exclude a directories' files, but not the directory 
 * itself, use something like <tt>./dir/§*</tt> or <tt>./dir/§**</tt>
 * 
 * 
 * \section ignpat_pcre PCRE-patterns
 * 
 * PCRE stands for Perl Compatible Regular Expressions; you can read about
 * them with <tt>man pcre</tt> (if the manpages are installed), and/or
 * asdfasdgasdgahsgkjashg <tt>perldoc perlre</tt> (if perldoc is installed)
 * 
 * These patterns have the form \c PCRE:{pattern} (with \c PCRE in
 * uppercase, to distinguish from modifiers).
 * 
 * An example:
 * \code
 *     PCRE:./home/.*~
 * \endcode
 * This one achieves exactly the same as <tt>./home/§**~</tt> .
 * 
 * Another example:
 * \code
 *     PCRE:./home/[a-s]
 * \endcode
 *
 * This would match \c /home/anthony , \c /home/guest , \c /home/somebody 
 * and so on, but would not match \c /home/theodore .
 * 
 * 
 * Note that the pathnames start with \c ./ , just like above, and that the
 * patterns are anchored at the beginning. To additionally anchor at the 
 * end you could use a <tt>$</tt> at the end.
 * 
 * 
 * \section ign_dev Ignoring all files on a device
 * 
 * Another form to discern what is needed and what not is possible with
 * <tt>DEVICE:[&lt;|&lt;=|&gt;|&gt;=]major[:minor]</tt>.
 * 
 * This takes advantage of the major and minor numbers of inodes (see <tt>
 * man 1 stat</tt> and <tt>man 2 stat</tt>).
 * 
 * The rule is as follows:
 * - Directories have their parent matched against the given string
 * - All other entries have their own device matched.
 * 
 * This is because the mount-point (ie. the directory, where the other
 * filesystem gets attached) should be versioned (as it's needed after
 * restore), but all entries (and all binding mounts) should not.
 * 
 * The possible options \c &lt;= or \c &gt;= define a less-or-equal-than 
 * respective bigger-or-equal-than relationship, to ignore a set of device 
 * classes.
 * 
 * Examples:
 * \code
 *     tDEVICE:3
 *     ./§*
 * \endcode
 * This patterns would define that all filesystems on IDE-devices (with 
 * major number 3) are \e taken , and all other files are	ignored.
 * 
 * \code
 *    DEVICE:0
 * \endcode
 * This would ignore all filesystems with major number 0 - in	linux these 
 * are the \e virtual filesystems ( \c proc , \c sysfs , \c devpts , etc.;  
 * see \c /proc/filesystems , the lines with \c nodev ).
 * 
 * Mind NFS and smb-mounts, check if you're using \e md , \e lvm and/or
 * \e device-mapper !
 * 
 * 
 * Note: The values are parsed with \c strtoul() , so you can use decimal,
 * hexadecimal (with \c 0x prepended) and octal (with \c 0 prepended)
 * notation.
 * 
 * 
 * \section ign_inode Ignoring a single file, by inode
 * 
 * At last, another form to ignore entries is to specify them via the
 * device their on and their inode:
 * \code
 *     INODE:major:minor:inode
 * \endcode
 * This can be used if a file can be hardlinked to many places, but only 
 * one copy should be stored. Then one path can be marked as to \e take , 
 * and other instances are ignored.
 * \note That's probably a bad example. There should be a better mechanism 
 * for handling hardlinks, but that needs some help from subversion.
 * 
 * 
 * 
 * \section ign_mod Modifiers
 * 
 * All of these patterns can have one or more of these modifiers *before*
 * them; not all combinations make sense.
 * 
 * 
 * <table>
 * <tr><th>Modifier<th>Meaning
 * <tr><th>i
 *   <td>Ignore case for matching
 * <tr><th>t
 *   <td>A negative ignore pattern, ie. a take pattern.
 * </table>
 *
 * \code
 *     t./proc/stat
 *     ./proc/
 * \endcode
 * Such
 * declaration would store \e only \c /proc/stat , and nothing else of \c 
 * /proc .
 * 
 * */


 /* They are only pointers */
#define RESERVE_IGNORE_ENTRIES (4)

/* number of entries */
static const char ign_header_str[] = "%u";

int max_ignore_len=0,
		max_ignore_entries=0,
		used_ignore_entries=0;

static struct ignore_t *ignore_list=NULL;

static char *memory;


/** Processes a character class in shell ignore patterns.
 * */
int ign___translate_bracketed_expr(char *end_of_buffer,
																	 char **src, char **dest)
{
	int status = 0;
	int pos_in_bracket_expr = -1; // zero-based, -1 == outside
	int backslashed = 0;


	STOPIF(**src != '[',
				 "invalid argument, **src does not point to "
				 "start of bracket expression");

	do
	{
		if (backslashed)
		{
			/* Escaped mode; blindly copy the next character. */
			*((*dest)++) = *((*src)++);
			backslashed = 0;
			/* pos_in_bracket_expr has already been increased. */
		}
		else if ( pos_in_bracket_expr == 0 &&
				(**src == '!' || **src == '^') )
		{
			*((*dest)++) = '^';
			++(*src);
			/* "!" or "^" at the start of a bracket expression (negation of the 
			 * bracket expression/character class) do not count as a regular 
			 * content element, so pos_in_bracket_expr is left alone. */
		}
		else
		{
			if (**src == ']' && pos_in_bracket_expr > 0)
			{
				/* Bracket expression ends. Set "end of expression"
				   marker and fall through to copy the closing bracket. */
				pos_in_bracket_expr = -1;
			}
			else
			{
				/* Now we're at the next character position. */
				++pos_in_bracket_expr; 
			}

			/* Enter escaped mode? */
			backslashed = (**src == '\\'); 

			*((*dest)++) = *((*src)++);
		}

		/* end_of_buffer points at character after the allocated destination 
		 * buffer space -- *end_of_buffer is invalid/undefined.
		 * Here we just have to be careful to not overwrite the stack - the 
		 * real length check is in ign__compile_pattern(). */
		STOPIF_CODE_ERR( end_of_buffer - *dest < 5, ENOSPC,
										 "not enough space in buffer");
	}
	while(**src && pos_in_bracket_expr >= 0);


ex:
	return status;
}


/** Compiles the given pattern for use with \c PCRE. 
 * */
int ign__compile_pattern(struct ignore_t *ignore)
{
	const char *err;
	int offset;
	int len;
	char *buffer;
	char *src, *dest;
	int status;
	int backslashed;


	status=0;
	if (ignore->type == PT_PCRE)
		dest=ignore->compare_string;
	else if (ignore->type == PT_SHELL)
	{
		ignore->has_wildwildcard=0;
		/* translate shell-like syntax into pcre */
		src=ignore->compare_string;
		len=strlen(ignore->pattern)*5+16;
		buffer=malloc(len);
		STOPIF_ENOMEM(!buffer);
		dest=buffer;
		backslashed = 0;
		do
		{
			if (backslashed)
			{
				// escaped mode
				*(dest++) = *(src++);
				backslashed = 0;
			}
			else
			{
			switch(*src)
			{
				case '*':
					if (src[1] == '*')
					{
						ignore->has_wildwildcard=1;
						/* anything */
						*(dest++) = '.';
						*(dest++) = '*';
						while (*src == '*') src++;
					}
					else
					{
						/* one directory level */
						*(dest++) = '[';
						*(dest++) = '^';
						*(dest++) = PATH_SEPARATOR;
						*(dest++) = ']';
						*(dest++) = '*';
						src++;
					}
					break;
				case '?':
					*(dest++) = '.';
					src++;
					break;
				case '[':
					// processed bracket expression and advanced src and dest pointers
					STOPIF(ign___translate_bracketed_expr(buffer + len, &src, &dest),
								 "processing a bracket expression failed");
					break;
				case '0' ... '9':
				case 'a' ... 'z':
				case 'A' ... 'Z':
				/* Note that here it's not a PATH_SEPARATOR, but the simple 
				 * character -- on Windows there'd be a \, which would trash the 
				 * regular expression! Although we'd have some of these problems on 
				 * Windows ...*/
				case '/': 
				case '-':
					*(dest++) = *(src++);
					break;
				case '\\':
					backslashed = 1; // enter escaped mode
					*(dest++) = *(src++);
					break;
					/* . and all other special characters { ( ] ) } + # " \ $
					 * get escaped. */
				case '.':
				default:
					*(dest++) = '\\';
					*(dest++) = *(src++);
					break;
			}
			}

			/* Ensure that there is sufficient space in the buffer to process the 
			 * next character. A "*" might create up to 5 characters in dest, the 
			 * directory matching patterns appended last will add up to five, and 
			 * we have a terminating '\0'. */
			STOPIF_CODE_ERR( buffer+len - dest < 11, ENOSPC,
					"not enough space in buffer");
		} while (*src);

		if (src != ignore->compare_string) 
		{
			*(dest++) = '$'; // anchor regexp

			/* src has moved at least one char, so it's safe to reference [-1] */
			if(src[-1] == PATH_SEPARATOR)
			{
				/* Ok, the glob pattern ends in a PATH_SEPARATOR, so our special 
				 * "ignore directory" handling kicks in. This results in "($|/)" at 
				 * the end. */
				dest[-2] = '(';
				*(dest++) = '|';
				*(dest++) = PATH_SEPARATOR;
				*(dest++) = ')';
			}
		}

		*dest=0;
		/* return unused space */
		buffer=realloc(buffer, dest-buffer+2);
		STOPIF_ENOMEM(!buffer);
		ignore->compare_string=buffer;
		dest=buffer;
	}
	else  /* pattern type */
	{
		BUG("unknown pattern type %d", ignore->type);
		/* this one's for gcc */
		dest=NULL;
	}

	DEBUGP("compiled \"%s\"", ignore->pattern);
	DEBUGP("    into \"%s\"", ignore->compare_string);

	/* compile */
	ignore->compiled = pcre_compile(dest,
			PCRE_DOTALL | PCRE_NO_AUTO_CAPTURE | PCRE_UNGREEDY | PCRE_ANCHORED |
			(ignore->is_icase ? PCRE_CASELESS : 0),
			&err, &offset, NULL);

	STOPIF_CODE_ERR( !ignore->compiled, EINVAL,
			"pattern <%s> not valid; error <%s> at offset %d.",
			ignore->pattern, err, offset);

	/* Patterns are used often - so it should be okay to study them.
	 * Although it may not help much?
	 * Performance testing! */
	ignore->extra = pcre_study(ignore->compiled, 0, &err);
	STOPIF_CODE_ERR( err, EINVAL,
			"pattern <%s> not studied; error <%s>.",
			ignore->pattern, err);

ex:
	return status;
}


/** Does all necessary steps to use the given \c ignore_t structure.
 * */
int ign___init_pattern_into(char *pattern, char *end, struct ignore_t *ignore)
{
	const char 
		pcre_prefix[]="PCRE:",
		dev_prefix[]="DEVICE:",
		inode_prefix[]="INODE:",
		norm_prefix[]="./";
	int status, stop;
	int mj, mn;
	char *cp;


	cp=pattern+strlen(pattern);
	if (!end || end>cp) end=cp;

	/* go over \n and other white space. These are not allowed 
	 * at the beginning of a pattern. */
	while (isspace(*pattern)) 
	{
		pattern++;
		STOPIF_CODE_ERR( pattern>=end, EINVAL, "pattern has no pattern");
	}

	/* This are the defaults: */
	ignore->pattern = pattern;
	ignore->is_ignore=1;
	ignore->is_icase=0;
	stop=0;
	while (!stop)
	{
		switch (*pattern)
		{
			case 't':
				ignore->is_ignore=0;
				break;
			case 'i':
				ignore->is_icase=1;
				break;
			default:
				stop=1;
				break;
		}

		if (!stop) 
		{
			pattern++;
			STOPIF_CODE_ERR( pattern>=end, EINVAL, 
					"pattern not \\0-terminated");
		}
	}
	
	STOPIF_CODE_ERR(!pattern, EINVAL, "pattern ends prematurely");
	DEBUGP("pattern: %ccase, %s",
			ignore->is_icase ? 'I' : ' ',
			ignore->is_ignore ? "ignore" : "take");

	if (strncmp(dev_prefix, pattern, strlen(dev_prefix)) == 0)
	{
		ignore->type=PT_DEVICE;
		ignore->compare_string = pattern;
		ignore->compare = PAT_DEV__UNSPECIFIED;
		pattern+=strlen(dev_prefix);

		stop=0;
		while (!stop)
		{
			switch (*pattern)
			{
				case '<': 
					ignore->compare |= PAT_DEV__LESS;
					break;
				case '=': 
					ignore->compare |= PAT_DEV__EQUAL;
					break;
				case '>': 
					ignore->compare |= PAT_DEV__GREATER;
					break;
				default:
					stop=1;
					break;
			}
			if (!stop) pattern++;
		}

		if (ignore->compare == PAT_DEV__UNSPECIFIED)
			ignore->compare = PAT_DEV__EQUAL;

		ignore->major=strtoul(pattern, &cp, 0);
		DEBUGP("device pattern: major=%d, left=%s", ignore->major, cp);
		STOPIF_CODE_ERR( cp == pattern, EINVAL, 
				"no major number found in %s", ignore->pattern);

		/* we expect a : here */
		if (*cp)
		{
			STOPIF_CODE_ERR( *(cp++) != ':', EINVAL,
					"expected ':' between major and minor number in %s",
					ignore->pattern);

			pattern=cp;
			ignore->minor=strtoul(pattern, &cp, 0);
			STOPIF_CODE_ERR( cp == pattern, EINVAL, 
					"no minor number found in %s", ignore->pattern);

			STOPIF_CODE_ERR( *cp, EINVAL, 
					"I don't want to see anything behind the minor number in %s!",
					ignore->pattern);
			ignore->has_minor=1; 
		}
		else 
		{
			ignore->minor=PAT_DEV__UNSPECIFIED;
			ignore->has_minor=0; 
		}
		status=0;
	}
	else if (strncmp(inode_prefix, pattern, strlen(inode_prefix)) == 0)
	{
		ignore->type=PT_INODE;
		ignore->compare_string = pattern;
		pattern+=strlen(inode_prefix);

		mj=strtoul(pattern, &cp, 0);
		STOPIF_CODE_ERR( cp == pattern || *(cp++) != ':', EINVAL,
				"no major number in %s?", ignore->pattern);

		pattern=cp;
		mn=strtoul(pattern, &cp, 0);
		STOPIF_CODE_ERR( cp == pattern || *(cp++) != ':', EINVAL,
				"no minor number in %s?", ignore->pattern);

		ignore->dev=MKDEV(mj, mn);

		pattern=cp;
		ignore->inode=strtoull(pattern, &cp, 0);
		STOPIF_CODE_ERR( cp == pattern || *cp!= 0, EINVAL,
				"garbage after inode in %s?", ignore->pattern);

		status=0;
	}
	else
	{
		if (strncmp(pcre_prefix, pattern, strlen(pcre_prefix)) == 0)
		{
			ignore->type=PT_PCRE;
			pattern += strlen(pcre_prefix);
			DEBUGP("pcre matching");
		}
		else if (strncmp(pattern, norm_prefix, strlen(norm_prefix)) == 0)
		{
			ignore->type=PT_SHELL;
			DEBUGP("shell pattern matching");
			/* DON'T pattern+=strlen(norm_prefix) - it's needed for matching ! */
		}
		else
			STOPIF_CODE_ERR(1, EINVAL, 
					"expected %s at beginning of pattern!", norm_prefix);


		STOPIF_CODE_ERR( strlen(pattern)<3, EINVAL,
			"pattern %s too short!", ignore->pattern);

		/* count number of PATH_SEPARATORs */
		cp=strchr(pattern, PATH_SEPARATOR);
		for(ignore->path_level=0;
				cp;
				ignore->path_level++)
			cp=strchr(cp+1, PATH_SEPARATOR);

		ignore->compare_string = pattern;
		status=ign__compile_pattern(ignore);
		STOPIF(status, "compile returned an error");
	}

ex:
	return status;
}


/** -.
 * */
int ign__load_list(char *dir)
{
	int status, fh, l;
	struct stat st;
	char *cp,*cp2;
	int count;


	fh=-1;
	status=waa__open_byext(dir, WAA__IGNORE_EXT, 0, &fh);
	if (status == ENOENT)
	{
		DEBUGP("no ignore list found");
		status=0;
		goto ex;
	}
	else STOPIF_CODE_ERR(status, status, "reading ignore list");

	STOPIF_CODE_ERR( fstat(fh, &st), errno, NULL);

	memory=mmap(NULL, st.st_size, 
			PROT_READ, MAP_SHARED, 
			fh, 0);
	/* If there's an error, return it.
	 * Always close the file. Check close() return code afterwards. */
	status=errno;
	l=close(fh);
	STOPIF_CODE_ERR( !memory, status, "mmap failed");
	STOPIF_CODE_ERR( l, errno, "close() failed");


	/* make header \0 terminated */
	cp=strchr(memory, '\n');
	if (!cp)
	{
		/* This means no entries.
		 * Maybe we should check?
		 */
		DEBUGP("Ignore list header is invalid.");
		status=0;
		goto ex;
	}

	status=sscanf(memory, ign_header_str, 
			&count);
	STOPIF_CODE_ERR( status != 1, EINVAL, 
			"ignore header is invalid");

	cp++;
	STOPIF( ign__new_pattern(count, NULL, NULL, 0, 0), NULL );


	/* fill the list */
	cp2=memory+st.st_size;
	for(l=0; l<count; l++)
	{
		STOPIF( ign__new_pattern(1, &cp, cp2, 1, PATTERN_POSITION_END), NULL);

		/* All loaded patterns are from the user */
		cp+=strlen(cp)+1;
		if (cp >= cp2) break;
	}

	if (l != count)
		DEBUGP("Ignore-list defect - header count (%u) bigger than actual number"
				"of patterns (%u)",
				count, l);
	if (cp >= cp2) 
		DEBUGP("Ignore-list defect - garbage after counted patterns");
	l=used_ignore_entries;

	status=0;

ex:
	/* to make sure no bad things happen */
	if (status)
		used_ignore_entries=0;

	return status;
}


/** Compares the given \c sstat_t \a st with the \b device ignore pattern 
 * \a ign.
 * Does the less-than, greater-than and/or equal comparision.
 * */
inline int ign___compare_dev(struct sstat_t *st, struct ignore_t *ign)
{
	int mj, mn;

	mj=(int)MAJOR(st->dev);
	mn=(int)MINOR(st->dev);

	if (mj > ign->major) return +2;
	if (mj < ign->major) return -2;

	if (!ign->has_minor) return 0;
	if (mn > ign->minor) return +1;
	if (mn < ign->minor) return -1;

	return 0;
}


/* Searches this entry for a take/ignore
 *
 * If a parent directory has an ignore entry which might be valid 
 * for this directory (like **§/§*~), it is mentioned in this
 * directory, too - in case of something like dir/a*§/b*§/§* 
 * a path level value is given.
 *
 * As we need to preserve the _order_ of the ignore/take statements,
 * we cannot easily optimize.
 * is_ignored is set to +1 if ignored, 0 if unknown, and -1 if 
 * on a take-list (overriding later ignore list).
 */
int ign__is_ignore(struct estat *sts,
		int *is_ignored)
{
	struct estat *dir;
	int status, namelen UNUSED, len, i, path_len UNUSED;
	char *path UNUSED, *cp;
	struct ignore_t **ign_list UNUSED;
	struct ignore_t *ign;
	struct sstat_t *st;
	struct estat sts_cmp;


	*is_ignored=0;
	status=0;
	dir=sts->parent;
	/* root directory won't be ignored */
	if (!dir) goto ex;

	if (ops___filetype(&(sts->st)) == FT_IGNORE)
	{
		*is_ignored=1;
		goto ex;
	}

	/* TODO - see ign__set_ignorelist() */ 
	/* currently all entries are checked against the full ignore list -
	 * not good performance-wise! */
	STOPIF( ops__build_path(&cp, sts), NULL);

	len=strlen(cp);
	for(i=0; i<used_ignore_entries; i++)
	{
		ign=ignore_list+i;

#if 0
		ign_list=dir->active_ign;
		if (!ign_list) goto ex;


		namelen=strlen(sts->name);
		path=NULL;
		while ( (ign = *ign_list) )
		{
			if (ign->path_level)
			{
				/* no need to calculate the full path if it's not used. */
				if (!path)
				{
					status=ops__build_path(&path, sts);
					if (status) goto ex;

					path_len=strlen(path);
				}

				/* search for the corresponding slash */
				i=ign->path_level;
				cp=path+path_len;
				while (cp >= path)
				{
					if (*cp == PATH_SEPARATOR)
						if (--i<0) break;

					cp--;
				}
				/* Pattern matches only for greater depth.
				 * It should not be mentioned here. */
				if (cp<path) 
				{
					DEBUGP("pattern \"%s\" matches only further below (currently at %s)!",
							ign->pattern, path);
#if 0
					BUG();
#else
					goto next;
#endif
				}

				/* now cp is >= path, with the specified number of directory levels
				 * deep */
				len=path+path_len-cp;
			}
			else
			{
				cp=sts->name;
				len=namelen;
			}
#endif

			if (ign->type == PT_SHELL || ign->type == PT_PCRE)
			{
				DEBUGP("matching %s against %s",
						cp, ign->pattern);
				status=pcre_exec(ign->compiled, ign->extra, 
						cp, len, 
						0, 0,
						NULL, 0);
				STOPIF_CODE_ERR( status && status != PCRE_ERROR_NOMATCH, 
						status, "cannot match pattern %s on data %s",
						ign->pattern, cp);
			}
			else if (ign->type == PT_DEVICE)
			{
				/* device compare */
				st=(S_ISDIR(sts->st.mode)) ? &(dir->st) : &(sts->st);

				switch (ign->compare)
				{
					case PAT_DEV__LESS:
						status= ign___compare_dev(st, ign) <  0;
						break;
					case PAT_DEV__LESS | PAT_DEV__EQUAL:
						status= ign___compare_dev(st, ign) <= 0;
						break;
				 	case PAT_DEV__EQUAL:
						status= ign___compare_dev(st, ign) == 0;
						break;
				 	case PAT_DEV__EQUAL | PAT_DEV__GREATER:
						status= ign___compare_dev(st, ign) >= 0;
						break;
					case PAT_DEV__GREATER:
						status= ign___compare_dev(st, ign) >  0;
						break;
				}

				/* status = 0 if *matches* ! */
				status = !status;
				DEBUGP("device compare pattern status=%d", status);
			}
			else if (ign->type == PT_INODE)
			{
				sts_cmp.st.dev=ign->dev;
				sts_cmp.st.ino=ign->inode;
				status = dir___f_sort_by_inodePP(&sts_cmp, sts) != 0;
				DEBUGP("inode compare %llX:%llu status=%d", 
						(t_ull)ign->dev, (t_ull)ign->inode, status);
			}
			else
				BUG("unknown pattern type 0x%X", ign->type);

			/* here status == 0 means pattern matches */
			if (status == 0) 
			{
				*is_ignored = ign->is_ignore ? +1 : -1;
				DEBUGP("pattern found -  result %d", *is_ignored);
				goto ex;
			}

#if 0
next:
			ign_list++;
		}
#endif
	}

	/* no match, no error */
	status=0;

ex:
	return status;
}


/** Writes the ignore list back to disk storage.
 * */
int ign__save_ignorelist(char *basedir)
{
	int status, fh, l, i;
	struct ignore_t *ign;
	char buffer[HEADER_LEN];


	DEBUGP("saving ignore list");
	fh=-1;
	STOPIF( waa__open_byext(basedir, WAA__IGNORE_EXT, 1, &fh), NULL);

	/* do header */
	for(i=l=0; i<used_ignore_entries; i++)
	{
		if (ignore_list[i].is_user_pat) l++;
	}

	status=snprintf(buffer, sizeof(buffer)-1,
			ign_header_str, 
			l);
	STOPIF_CODE_ERR(status >= sizeof(buffer)-1, ENOSPC,
		"can't prepare header to write; buffer too small");

	strcat(buffer, "\n");
	l=strlen(buffer);
	status=write(fh, buffer, l);
	STOPIF_CODE_ERR( status != l, errno, "error writing header");


	/* write data */
	ign=ignore_list;
	for(i=0; i<used_ignore_entries; i++)
	{
		if (ignore_list[i].is_user_pat)
		{
			l=strlen(ign->pattern)+1;
			status=write(fh, ign->pattern, l);
			STOPIF_CODE_ERR( status != l, errno, "error writing data");

			status=write(fh, "\n", 1);
			STOPIF_CODE_ERR( status != 1, errno, "error writing newline");
		}

		ign++;
	}

	status=0;

ex:
	if (fh!=-1) 
	{
		l=waa__close(fh, status);
		fh=-1;
		STOPIF(l, "error closing ignore data");
	}

	return status;
}


int ign__new_pattern(unsigned count, char *pattern[], 
		char *ends,
		int user_pattern,
		int position)
{
	int status;
	unsigned i;
	struct ignore_t *ign;


	status=0;
	DEBUGP("getting %d new entries - max is %d, used are %d", 
			count, max_ignore_entries, used_ignore_entries);
	if (used_ignore_entries+count >= max_ignore_entries)
	{
		max_ignore_entries = used_ignore_entries+count+RESERVE_IGNORE_ENTRIES;
		ignore_list=realloc(ignore_list, 
				sizeof(*ignore_list) * max_ignore_entries);
		STOPIF_ENOMEM(!ignore_list);
	}


	/* If we're being called without patterns, we should just reserve 
	 * the space in a piece. */
	if (!pattern) goto ex;


	/* Per default new ignore patterns are appended. */
	if (position != PATTERN_POSITION_END && used_ignore_entries>0)
	{
		/* This would be more efficient with a list of pointers.
		 * But it happens only on explicit user request, and is therefore
		 * very infrequent. */
		/* This code currently assumes that all fsvs-system-patterns are
		 * at the front of the list. The only use is currently in waa__init(),
		 * and so that should be ok. */
		/* If we assume that "inserting" patterns happen only when we don't
		 * do anything but read, insert, write, we could even put the new
		 * patterns in front.
		 * On writing only the user-patterns would be written, and so on the next
		 * load the order would be ok. */

		/* Find the first user pattern, and move from there. */
		for(i=0; i<used_ignore_entries; i++)
			if (ignore_list[i].is_user_pat) break;

		/* Now i is the index of the first user pattern, if any.
		 * Before:
		 *   used_ignore_entries=7
		 *   [ SYS SYS SYS User User User User ]
		 *                  i=3
		 * Then, with count=2 new patterns, at position=0:
		 *   [ SYS SYS SYS Free Free User User User User ]
		 *                  i
		 * So we move from ignore_list+3 to ignore_list+5, 4 Elements.
		 * QED :-) */
		i+=position;

		memmove(ignore_list+i+count, ignore_list+i, 
				(used_ignore_entries-i)*sizeof(*ignore_list));
		/* Where to write the new patterns */
		position=i;
	}
	else
		position=used_ignore_entries;

	BUG_ON(position > used_ignore_entries || position<0);

	status=0;
	for(i=0; i<count; i++)
	{
		/* This prints the newline, so debug output is a bit mangled.
		 * Doesn't matter much, and whitespace gets removed in 
		 * ign___init_pattern_into(). */
		DEBUGP("new pattern %s", *pattern);
		ign=ignore_list+i+position;

		/* We have to stop on wrong patterns; at least, if we're
		 * trying to prepend them.
		 * Otherwise we'd get holes in ignore_list, which we must not write. */
		STOPIF( ign___init_pattern_into(*pattern, ends, ign), NULL);

		ign->is_user_pat=user_pattern;
		pattern++;
	}

	used_ignore_entries+=count;

ex:
	return status;
}


/* This is called to append new ignore patterns.
 **/
int ign__work(struct estat *root UNUSED, int argc, char *argv[])
{
	int status;
	int position, i;
	char *cp, *copy;


	status=0;

	/* A STOPIF_CODE_ERR( argc==0, 0, ...) is possible, but not very nice -
	 * the message is not really user-friendly. */
	if (argc==0)
		ac__Usage_this();

	/* Now we can be sure to have at least 1 argument. */

	/* Goto correct base. */
	status=waa__find_common_base(0, NULL, NULL);
	if (status == ENOENT)


	DEBUGP("first argument is %s", argv[0]);

	status=0;
	if (strcmp(argv[0], parm_load) == 0)
	{
		i=0;
		while (1)
		{
			status=hlp__string_from_filep(stdin, &cp, 1);
			if (status == EOF) break;

			copy=strdup(cp);
			STOPIF( ign__new_pattern(1, &copy, NULL, 
						1, PATTERN_POSITION_END), NULL);
			i++;
		}

		if (opt_verbose>=0)
			printf("%d pattern%s loaded.\n", i, i==1 ? "" : "s");
	}
	else
	{
		/* We edit or dump the list, so read what we have. */
		STOPIF( ign__load_list(NULL), NULL);

		if (strcmp(argv[0], parm_dump) == 0)
		{
			/* Dump only user-patterns. */
			for(i=position=0; i < used_ignore_entries; i++, position++)
				if (ignore_list[i].is_user_pat)
				{
					if (opt_verbose>0) 
						printf("%3d: ", position);

					printf("%s\n", ignore_list[i].pattern);
				}

			/* No need to save. */
			goto ex;
		}
		else 
		{
			/* Normal pattern inclusion. May have a position specification here. */
			position=PATTERN_POSITION_END;
			if (strcmp(argv[0], "prepend") == 0)
			{
				argv++;
				argc--;
				position=PATTERN_POSITION_START;
			}
			else if (sscanf(argv[0], "at=%d", &i) == 1)
			{
				argv++;
				argc--;
				STOPIF_CODE_ERR(i > used_ignore_entries, EINVAL,
						"The position %d where the pattern "
						"should be inserted is invalid.\n", i);
				position=i;
			}
			else if (strcmp(argv[0], "append") == 0)
			{
				/* Default */
				argv++;
				argc--;
			}


			STOPIF( ign__new_pattern(argc, argv, NULL, 1, position), NULL);
		}
	} /* not "fsvs load" */

	STOPIF( ign__save_ignorelist(NULL), NULL);

ex:
	return status;
}


inline int ign___do_parent_list(struct ignore_t ***target, int next_index,
		struct ignore_t **source,
		struct estat *sts,
		char *path, int pathlen)
{
	int all_parent, take;
	struct ignore_t **list;


	if (!source) goto ex;

	list=source;
	all_parent=1;
	while (*list)
	{
		take= (*list)->has_wildwildcard ?
			sts->path_level >= (*list)->path_level :
			sts->path_level == (*list)->path_level;

		if (take)
		{
			(*target)[next_index] = *list;
			next_index++;
		}
		else
			all_parent=0;

		list++;
	}

	/* Same entries as parent? Copy pointer, save memory */
	if (all_parent)
	{
		IF_FREE(*target);
		*target=source;
	}

ex:
	return next_index;
}

/* Here we have to find the possibly matching entries.
 * All entries of the parent directory are looked at,
 * and the possible subdirectory-entries of the parent.
 *
 * Patterns on the active list define patterns for this and lower
 * levels; they may or may not be needed for the sub-entry.
 *
 * Patterns of the subdir list have a specified minimum level;
 * these may be applicable here, and possibly below. */
int ign__set_ignorelist(struct estat *sts)
{
	BUG_ON(!S_ISDIR(sts->st.mode));
	/* TODO TODO - see below */
	return 0;

#if 0
	int status,i, act, sub;
	struct estat *parent;
	char *path;
	int pathlen;


	IF_FREE(sts->active_ign);
	IF_FREE(sts->subdir_ign);

	/* NULL terminated */
	sts->active_ign=calloc(used_ignore_entries+1, sizeof(*sts->active_ign));
	sts->subdir_ign=calloc(used_ignore_entries+1, sizeof(*sts->subdir_ign));
	STOPIF_ENOMEM(!sts->active_ign || !sts->subdir_ign);

	act=sub=0;

	STOPIF( ops__build_path(&path, sts), NULL);
	pathlen=strlen(path);


	parent=sts->parent;
	if (parent) 
	{
		act=ign___do_parent_list(	&(sts->active_ign), 0,
				parent->active_ign,
				sts,
				path, pathlen);

		/* doesn't work this way --- TODO TODO TODO */
		act=ign___do_parent_list(	&(sts->subdir_ign), act,
				parent->active_ign,
				sts,
				path, pathlen);
	}
	else
	{
		for(i=0; i<used_ignore_entries; i++)
		{
			/* TODO TODO TODO */
		}
	}

	/* for 0 elements we could let realloc give us as NULL pointer -
	 * but I like that to be explicit.
	 * [We'd have to change that (act+1), too.] */
	if (act)
		sts->active_ign=realloc(sts->active_ign, 
				(act+1)* sizeof(*sts->active_ign) );
	else
		IF_FREE(sts->active_ign);

	if (sub)
		sts->subdir_ign=realloc(sts->subdir_ign,
				(sub+1)* sizeof(*sts->subdir_ign) );
	else
		IF_FREE(sts->subdir_ign);


	status=0;

ex:
	return status;
#endif
}

