/**
 * @file
 * Defines the SktObjTypeRegistry template class.
 */

template<class SktHndType, class SktObjType>
inline SktHndType SktObjTypeRegistry<SktHndType, SktObjType>::toHandle(
    SktObjType* sktObj)
{
    return reinterpret_cast<SktHndType>(sktObj);
}

template<class SktHndType, class SktObjType>
void SktObjTypeRegistry<SktHndType, SktObjType>::addSocketObj(
    const SktHndType& sktHnd, const boost::shared_ptr<SktObjType>& sktObj)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    std::pair<std::map<SktHndType, boost::shared_ptr<SktObjType> >::iterator,
        bool> insertResult = m_hndToSktObjMap.insert(
        std::make_pair(sktHnd, sktObj));
    assert(insertResult.second); // Socket cannot already exist in registry
}

template<class SktHndType, class SktObjType> boost::shared_ptr<SktObjType>
SktObjTypeRegistry<SktHndType, SktObjType>::findSocketObj(SktHndType sktHnd)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    boost::shared_ptr<SktObjType> sktObj;
    std::map<SktHndType, boost::shared_ptr<SktObjType> >::iterator it =
        m_hndToSktObjMap.find(sktHnd);
    if (it != m_hndToSktObjMap.end())
    {
        sktObj = it->second;
    }
    return sktObj;
}

template<class SktHndType, class SktObjType> boost::shared_ptr<SktObjType>
SktObjTypeRegistry<SktHndType, SktObjType>::removeSocketObj(SktHndType sktHnd)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    boost::shared_ptr<SktObjType> sktObj;
    std::map<SktHndType, boost::shared_ptr<SktObjType> >::iterator it =
        m_hndToSktObjMap.find(sktHnd);
    if (it != m_hndToSktObjMap.end())
    {
        sktObj = it->second;
        m_hndToSktObjMap.erase(it);
    }
    return sktObj;
}

template<class SktHndType, class SktObjType> boost::shared_ptr<SktObjType>
SktObjTypeRegistry<SktHndType, SktObjType>::removeFrontSocketObj()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    boost::shared_ptr<SktObjType> sktObj;
    std::map<SktHndType, boost::shared_ptr<SktObjType> >::iterator it =
        m_hndToSktObjMap.begin();
    if (it != m_hndToSktObjMap.end())
    {
        sktObj = it->second;
        m_hndToSktObjMap.erase(it);
    }
    return sktObj;
}
