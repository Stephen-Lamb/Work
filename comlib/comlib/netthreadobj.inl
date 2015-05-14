/**
 * @file
 * Inline method definitions for the NetThreadObj class.
 */

inline bool NetThreadObj::isShutdown() const
{
    return (WaitForSingleObject(m_isShutdownEvent, 0) == WAIT_OBJECT_0);
}
