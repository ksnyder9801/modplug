/*
 * PatternContainer.h
 * ------------------
 * Purpose: Container class for managing patterns.
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once

#include "pattern.h"

#include <algorithm>

OPENMPT_NAMESPACE_BEGIN

class CSoundFile;
typedef CPattern MODPATTERN;

//=====================
class CPatternContainer
//=====================
{
//BEGIN: TYPEDEFS
public:
	typedef std::vector<MODPATTERN> PATTERNVECTOR;
//END: TYPEDEFS


//BEGIN: OPERATORS
public:
	//To mimic old pattern == ModCommand* behavior.
	MODPATTERN& operator[](const int pat) {return m_Patterns[pat];}
	const MODPATTERN& operator[](const int pat) const {return m_Patterns[pat];}
//END: OPERATORS

//BEGIN: INTERFACE METHODS
public:
	CPatternContainer(CSoundFile& sndFile) : m_rSndFile(sndFile) {m_Patterns.assign(MAX_PATTERNS, MODPATTERN(*this));}

	// Clears existing patterns and resizes array to default size.
	void Init();

	// Empty and initialize all patterns.
	void ClearPatterns();
	// Delete all patterns.
	void DestroyPatterns();
	
	//Insert (default)pattern to given position. If pattern already exists at that position,
	//ignoring request. Returns true on failure, false otherwise.
	bool Insert(const PATTERNINDEX index, const ROWINDEX rows);
	
	//Insert pattern to position with the lowest index, and return that index, PATTERNINDEX_INVALID
	//on failure.
	PATTERNINDEX Insert(const ROWINDEX rows);

	// Duplicate an existing pattern. Returns new pattern index on success, or PATTERNINDEX_INVALID on failure.
	PATTERNINDEX Duplicate(PATTERNINDEX from);

	//Remove pattern from given position. Currently it actually makes the pattern
	//'invisible' - the pattern data is cleared but the actual pattern object won't get removed.
	bool Remove(const PATTERNINDEX index);

	// Applies function object for modcommands in patterns in given range.
	// Return: Copy of the function object.
	template <class Func>
	Func ForEachModCommand(PATTERNINDEX nStartPat, PATTERNINDEX nLastPat, Func func);
	template <class Func>
	Func ForEachModCommand(Func func) {return ForEachModCommand(0, Size() - 1, func);}

	PATTERNINDEX Size() const {return static_cast<PATTERNINDEX>(m_Patterns.size());}

	CSoundFile& GetSoundFile() {return m_rSndFile;}
	const CSoundFile& GetSoundFile() const {return m_rSndFile;}

	//Returns the index of given pattern, Size() if not found.
	PATTERNINDEX GetIndex(const MODPATTERN* const pPat) const;

	// Return true if pattern can be accessed with operator[](iPat), false otherwise.
	bool IsValidIndex(const PATTERNINDEX iPat) const {return (iPat < Size());}

	// Return true if IsValidIndex() is true and the corresponding pattern has allocated modcommand array, false otherwise.
	bool IsValidPat(const PATTERNINDEX iPat) const {return IsValidIndex(iPat) && (*this)[iPat];}

	// Returns true if the pattern is empty, i.e. there are no notes/effects in this pattern
	bool IsPatternEmpty(const PATTERNINDEX nPat) const;
	
	void ResizeArray(const PATTERNINDEX newSize);

	void OnModTypeChanged(const MODTYPE oldtype);

	// Returns index of last valid pattern + 1, zero if no such pattern exists.
	PATTERNINDEX GetNumPatterns() const;

	// Returns index of highest pattern with pattern named + 1.
	PATTERNINDEX GetNumNamedPatterns() const;

//END: INTERFACE METHODS


//BEGIN: DATA MEMBERS
private:
	PATTERNVECTOR m_Patterns;
	CSoundFile &m_rSndFile;
//END: DATA MEMBERS

};


template <class Func>
Func CPatternContainer::ForEachModCommand(PATTERNINDEX nStartPat, PATTERNINDEX nLastPat, Func func)
//-------------------------------------------------------------------------------------------------
{
	if (nStartPat > nLastPat || nLastPat >= Size())
		return func;
	for (PATTERNINDEX nPat = nStartPat; nPat <= nLastPat; nPat++) if (m_Patterns[nPat])
		std::for_each(m_Patterns[nPat].Begin(), m_Patterns[nPat].End(), func);
	return func;
}


const char FileIdPatterns[] = "mptPc";

void ReadModPatterns(std::istream& iStrm, CPatternContainer& patc, const size_t nSize = 0);
void WriteModPatterns(std::ostream& oStrm, const CPatternContainer& patc);


OPENMPT_NAMESPACE_END
