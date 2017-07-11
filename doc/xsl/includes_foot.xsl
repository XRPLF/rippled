<!-- INCLUDES_FOOT_TEMPLATE BEGIN -->
  <xsl:choose>
    <xsl:when test="contains($file, 'beast/core')">
      <xsl:text>&#xd;&#xd;Convenience header [include_file beast/core.hpp]&#xd;</xsl:text>
    </xsl:when>
    <xsl:when test="contains($file, 'beast/http')">
      <xsl:text>&#xd;&#xd;Convenience header [include_file beast/http.hpp]&#xd;</xsl:text>
    </xsl:when>
    <xsl:when test="contains($file, 'beast/websocket')">
      <xsl:text>&#xd;&#xd;Convenience header [include_file beast/websocket.hpp]&#xd;</xsl:text>
    </xsl:when>
    <xsl:when test="contains($file, 'beast/zlib')">
      <xsl:text>&#xd;&#xd;Convenience header [include_file beast/zlib.hpp]&#xd;</xsl:text>
    </xsl:when>
  </xsl:choose>
<!-- INCLUDES_FOOT_TEMPLATE END -->
