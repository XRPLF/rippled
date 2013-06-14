
std::ostream& operator<< (std::ostream& out, const SHAMapMissingNode& mn)
{
    switch (mn.getMapType ())
    {
    case smtTRANSACTION:
        out << "Missing/TXN(" << mn.getNodeID () << "/" << mn.getNodeHash () << ")";
        break;

    case smtSTATE:
        out << "Missing/STA(" << mn.getNodeID () << "/" << mn.getNodeHash () << ")";
        break;

    case smtFREE:
    default:
        out << "Missing/" << mn.getNodeID ();
        break;
    };

    return out;
}
