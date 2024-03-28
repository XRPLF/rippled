//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2016 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/misc/ValidatorSite.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/strHex.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <chrono>
#include <date/date.h>
#include <test/jtx.h>
#include <test/jtx/TrustedPublisherServer.h>
#include <test/unit_test/FileDirGuard.h>

namespace ripple {
namespace test {
namespace detail {
constexpr const char*
realValidatorContents()
{
    return R"vl({
        "public_key": "ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F4734",
        "manifest": "JAAAAAFxIe0md6v/0bM6xvvDBitx8eg5fBUF4cQsZNEa0bKP9z9HNHMh7V0AnEi5D4odY9X2sx+cY8B3OHNjJvMhARRPtTHmWnAhdkDFcg53dAQS1WDMQDLIs2wwwHpScrUnjp1iZwwTXVXXsaRxLztycioto3JgImGdukXubbrjeqCNU02f7Y/+6w0BcBJA3M0EOU+39hmB8vwfgernXZIDQ1+o0dnuXjX73oDLgsacwXzLBVOdBpSAsJwYD+nW8YaSacOHEsWaPlof05EsAg==",
        "blob" : "eyJzZXF1ZW5jZSI6MzcsImV4cGlyYXRpb24iOjU5NDE3MjgwMCwidmFsaWRhdG9ycyI6W3sidmFsaWRhdGlvbl9wdWJsaWNfa2V5IjoiRUQ0OUY4RUI4MDcxRTI2RDRFMDY3MjM4RDYyRDY3Q0E2RUJGQjI5OEI0QTcyRENGQUQ1MzZBN0VDODkyNUM1MTlCIiwibWFuaWZlc3QiOiJKQUFBQUFGeEllMUorT3VBY2VKdFRnWnlPTll0WjhwdXY3S1l0S2N0ejYxVGFuN0lrbHhSbTNNaEFvZ01tUVRSQUZLLyttL1hXUWZKdlhUZU1jYWl5RjZna2hnQXNzZExPY2lJZGtZd1JBSWdOeFlhdkpQeUhBTnV4b2dRWTdSTkJQOXRab2daSnhGMXdacXptNGEzN0dnQ0lCT3ZiZVE4K1A0NkNDSWN5NFpDWkE1clpaKzkzSDBhS0paUTMrSUEwT091Y0JKQUFTVHBRS3RFSmtlcXdIeitqS1dOdldsMHNIUlFaU1NKdVlpTXl5VWE3eXRwc2V2Nk5Hb0ZuMWJOcFlUZ3ZzYVB4b3o3dkhza0RoMlo2Z2ViU0RDUkNBPT0ifSx7InZhbGlkYXRpb25fcHVibGljX2tleSI6IkVEODEwOEY3RDlFRTc3OURDMTA3NUVDM0NGOEVCOTAwNERERjg0Q0QwQTBGRjU3NzVDOUUzNzVDQ0FDREFBMzAyMiIsIm1hbmlmZXN0IjoiSkFBQUFBRnhJZTJCQ1BmWjduZWR3UWRldzgrT3VRQk4zNFROQ2cvMWQxeWVOMXpLemFvd0luTWhBbHZQSkFOUWNSVVNOVVIrTmxadW80a0dwWUY4bDlwSFBQMVpST09VY05ZUmRrWXdSQUlnUldaWnVKTEFmM1NOTUdvRnlnVWx1K2VBRDUxWm9IRk1jeW9pY24wV0t1Y0NJR3c0bjljVkRQT0JHQzh3bEpEeTkyODJ4OVJkT3dvMWNvMmcrTlpsYy9ETmNCSkFLSmhLL2FtNkw3S0U0NTBOVnpocFZWb0w4T0ZNbzBtRnhrOEJERzZRTzJjU05NTjNPeXdmNDBEK0lsZXM5TFh4eHZ2UEI2Z1dTbVBsd0Y3ZE5SOUdBUT09In0seyJ2YWxpZGF0aW9uX3B1YmxpY19rZXkiOiJFRDdFM0JBRkY5MDFERTUyNUIwMEIyQzFGRTE5QUY0NDlBMDgwQjVGMzEwMEM2RUMxODJDODY3QUY2MUY3MTBGRkQiLCJtYW5pZmVzdCI6IkpBQUFBQUZ4SWUxK082LzVBZDVTV3dDeXdmNFpyMFNhQ0F0Zk1RREc3QmdzaG5yMkgzRVAvWE1oQTUrR0RKaDJBcFdLQ2gyTjVvbXg0RkFPRkVxWE1qcXkxSEo1RTdXQTNGck9ka2N3UlFJaEFNUmV6d2pUQUwzNWpwamoxNWpxb0hVdlJtNys3aUhwVTQ3YUtFNkhFQ2haQWlCU01JWmFNV3Y2cU5KSG5pWXBzWUh4NE9QUHBCb0NNTWRNVkFHZkZpOWZLM0FTUUUwVFlpSXNHdjAveWxwcUdFQmlMa2syWGpyQTgrK0FrenByOXZjVHRya0hpRERvMGNIS085bVJVTEFYZXNSck95RmI1UWNPMGwwVnN3ZXZENWpsc3drPSJ9LHsidmFsaWRhdGlvbl9wdWJsaWNfa2V5IjoiRUQ0NUU4MEEwNEQ3OUNCOURGMDBBRUJEODZEQ0RDMTY4NkQ2NDE5RUE5RTVFMEU3MUYxQTgxN0UwOEI1MDc2QTU1IiwibWFuaWZlc3QiOiJKQUFBQUFGeEllMUY2QW9FMTV5NTN3Q3V2WWJjM0JhRzFrR2VxZVhnNXg4YWdYNEl0UWRxVlhNaEF4Wm8xNTdwY0I5ZGU2U21rN2hvSzN3TkNBcjRhRlp0ZkFQaTdDRTRtTkpsZGtjd1JRSWhBTGxWalhDZml5L210WEJXc050Nzd0NGpLY05FQnBSVjh6ditTcFU1bENoMEFpQmE4dm84eHhwdmlZbGY0emRHK25RaEIyT2dma1FaWlBNSE90N0NhWHpYZ1hBU1FMOE81cDA4M21nNEtLTDh1WmZNYVVxZGd6dUowR3RhMWx5VVdQY3RUUEN4WTEzNVh3SytuSkFkRnNJVUZOSjlNUGpucENtU2pZVnpWYTZNNS9uQWNBST0ifSx7InZhbGlkYXRpb25fcHVibGljX2tleSI6IkVERDhDODg2NDI3OTVDRTY5QzVCNzgwRTAxNzAyQzM3MEY5NTA3RDBCNjQ0MzNGMTdFRkU3MEYyNjM3QTQwQURCNyIsIm1hbmlmZXN0IjoiSkFBQUFBRnhJZTNZeUlaQ2VWem1uRnQ0RGdGd0xEY1BsUWZRdGtRejhYNytjUEpqZWtDdHQzTWhBbkZmcityOUJYZHNYRS9jQmxKTXlkL1hzTzFBNVhFWUNjdHJzdkxFWCtEbWRrY3dSUUloQU5SY1JNZzlTQVhvYU92SERaMmF2OVJ6RWFaYVZFTmZRaVZnc2krT3gzRjBBaUIyc25TSU9tNmM0L2luYnRVMFVtV0xRVHp1d2tPZFVGUElCOEF4OGRtR3VIQVNRTVVJZlhNajk2a2NGVFNKbk1GQy9tVy9BUThiS1hrRnJyazBDVVRGRkt3ZUVqVHErU1RyRmk2cUxMMk1UN252ZUd4c1hCQ2d6dGpjMHFHYXM5S0ZXZ009In0seyJ2YWxpZGF0aW9uX3B1YmxpY19rZXkiOiJFREJERUI5MDFGN0M3NUQwRTIwQzZDNDJBRjAzQkUwREE0MDM3N0FGMTkzOUExOEIzQ0IzNjc5NjYxREQ1RjlGNzQiLCJtYW5pZmVzdCI6IkpBQUFBQUZ4SWUyOTY1QWZmSFhRNGd4c1FxOER2ZzJrQTNldkdUbWhpenl6WjVaaDNWK2ZkSE1oQWczY3lLTlBNUHFLZ1I3a0lpN2MvOEdML1lnZEJ0ZzRtU0FXdndtYWV2Vkdka1l3UkFJZ1d6RzhHcVlnM1lwd0RzOHhYYTlYcUxIc3M3NktUMnVBSFJoVVhGVlVxQ1FDSUcyRXZiRktueGV6UmQ5Y3BQSFN0MzJIWEsrUDQrYUwzcDIrdnFsQ3hSUjljQkpBYm9YVG1ZVGF5b2NBM3pmOWRXRVh0eWFlT0dDMWs1V2RZVVJ6UGxlZXZ2YWxSNHhWb1h6czM4aUdQeEZyL3BBOW5MK000ZHV1MEdLQ0hsVmlyK2ZCQWc9PSJ9LHsidmFsaWRhdGlvbl9wdWJsaWNfa2V5IjoiRURBMTc4NzFFNzJCMEM1NzBBQzQzNDVDNjBDRjAyQUZCQkI3NDBBNjMxQjdBRDBFMUU1NzMyMTY1NzREOUFFQTAyIiwibWFuaWZlc3QiOiJKQUFBQUFGeEllMmhlSEhuS3d4WENzUTBYR0RQQXErN3QwQ21NYmV0RGg1WE1oWlhUWnJxQW5NaEFvanl1emd0cmVRa3hRajhwckh4T3NiRGNGNWZ1NFhYYjBLeEVML1BxNUhoZGtjd1JRSWhBTmZQRExaUDQ3YUNXd3Q1a0JucDc1QnV1Q2dwOWM0QmZKUGQ2NlNGQ3c2MUFpQUp2ZWdCdnZQSXJlYytYT1N6S1JmaTV1dVhXeHRsOUV5cjJhUEJZWHZiUkhBU1FNVUxZRW83YmVSZm9VQ25qazFzVFl5WTkxdExJR0xnbm5hV1hoVW04MCt6czVJR2VnazhxaWpLQXRCT011QkM3MWxBQjRLaEpjK2RCMnJwTU9GYzVndz0ifSx7InZhbGlkYXRpb25fcHVibGljX2tleSI6IkVERjQ2RUUyN0FEMEUxQTcxNEFGRUNEQTgxNkVBQjcxMTQ2MTRGQ0I5MkQwQ0I0RDk3QjZBODhFRDQzNDM0QUZDOSIsIm1hbmlmZXN0IjoiSkFBQUFBRnhJZTMwYnVKNjBPR25GSy9zMm9GdXEzRVVZVS9Ma3RETFRaZTJxSTdVTkRTdnlYTWhBdzBBVFdqVlR0NEZmZUtPN2t2NmZGZ2QvZ28yK2Q1QlN5VWNVUm1SV25UdGRrY3dSUUloQU13T2dEZWM3UVlZTm5nc3BnOTB3RXZWYnNvaDJ1eDE0UlBUdytHSGFYTmxBaUFMZ2ZFc3orQUY0ZXlYL1k1aTQ0VnJGakZGSU1XVWZPWmFRSnRzeHRlTTFYQVNRTE9hRjB0MlpwcVZLZDhKRVNRVlkrelU1NjdpQUFHMmFtVFBaeDk1ODc1UzlBNlBsK2tINVRHSE1BZVdqZ1dTcWZoM20ySEJKWDdOSWNYYjk4dnk5QUE9In0seyJ2YWxpZGF0aW9uX3B1YmxpY19rZXkiOiJFRDZFNEM0MUU1OUZGQkVCNTE3MjZFNTQ0Njg1MDJGRTY0MzcyMzhGQTc4RUE1MTYzNEU3QkYwRDA5MTcxQUVFOEYiLCJtYW5pZmVzdCI6IkpBQUFBQUZ4SWUxdVRFSGxuL3ZyVVhKdVZFYUZBdjVrTnlPUHA0NmxGalRudncwSkZ4cnVqM01oQXV6dEdXYi9PaTEvVjVtNWR1aldyOUhtYktSeUs0WFlrK2ttdUZQU2dBRnJka1l3UkFJZ2ZRK0JnWFg2UWJsWnk0SDA1bzdHUFNJd3FTN1FRUlVXN2RxRjU0SUFpaU1DSUg0WGZMdzk1NmlFYW94Wk9rN0tjdGluMlg5aE1mYUxON3d5czl5QVVGb1pjQkpBdWVFaTg0WFIzTGwxR0xKV2FuVzFnMU1kVWovMFBBeEpidzZFRVFSdUczemRudVJITlhsZDZVWkFiSWtWY1AwenRmcXVsQnpqYmNzTERPS0ZFaWNTQmc9PSJ9LHsidmFsaWRhdGlvbl9wdWJsaWNfa2V5IjoiRURCNkZDOEU4MDNFRThFREMyNzkzRjFFQzkxN0IyRUU0MUQzNTI1NTYxOERFQjkxRDNGOUIxRkM4OUI3NUQ0NTM5IiwibWFuaWZlc3QiOiJKQUFBQUFGeEllMjIvSTZBUHVqdHduay9Ic2tYc3U1QjAxSlZZWTNya2RQNXNmeUp0MTFGT1hNaEE4VmR2SEZ5U2NCeVFHVFlOR2VPdkIwKzY3Z1dhcWVmY2Z2Ums1K0t3Z1YxZGtZd1JBSWdaRnVsTy9BaU1vY3puZzZpLzRCa2Z6VDdqOWx4RjRQUDF1ZmdyT1FhSjhzQ0lCWC9FOFpicG43dFdxZ0F5TnlXcFZQa2hGbWFVTXFFcnk4V29VVDFmZEdRY0JKQXY1MVJxSnhnZy9Wcm5yWndpTEsyRGMwQ0tiaUxQTzVISjRaTXNqZFBUMmdSYzk3cldrQVh1VjJMNlBORk81OXh5dW9hWm1TTWxaWXZxU0dQcGZGN0J3PT0ifSx7InZhbGlkYXRpb25fcHVibGljX2tleSI6IkVENjkxMzAzOTkyRkVDNjRFNkJDNEJBQ0QzNkFFNkU1QUVEQzIzRjI4NjFCNkQ4RUZCOUZENzdFRTNFQURFMzQzNSIsIm1hbmlmZXN0IjoiSkFBQUFBRnhJZTFwRXdPWkwreGs1cnhMck5OcTV1V3UzQ1B5aGh0dGp2dWYxMzdqNnQ0ME5YTWhBaTJBWEpRZ28vSnVXM3I3Zi82Q2NWc0dOMVltSWoxMUdpSUVTSEJuUVNrOGRrY3dSUUloQU5DREVReW1yZDZ2ZVQzb3VhY0Y2ZmhCcjV3THczR21YZzFyTUNMVnZCelpBaUE4dVdRK3RxZDQ2V21mQmV4alNCUTJKZDZVQUdkckh2amNDUTJaZ1Nvb0NuQVNRRmtIbCtENy9VM1dCeVlQMzg0K3BjRkRmMkdpNFdJUkhWVG81OGNxZGs1Q0Rpd2MxVDByRG9MaG1vNDFhM2YrZHNmdGZ3UjRhTW13RmNQWExucmpyQUk9In0seyJ2YWxpZGF0aW9uX3B1YmxpY19rZXkiOiJFREFEMTY2NjdGMDE4NUREQkI3RkE2NUIyMkY0QjdEMzEwQkY1QzNFMkQ5QjgyM0ZCMDZBM0E0MUFGOEFDODNCQzEiLCJtYW5pZmVzdCI6IkpBQUFBQUZ4SWUydEZtWi9BWVhkdTMrbVd5TDB0OU1RdjF3K0xadUNQN0JxT2tHdmlzZzd3WE1oQXF3ZUUzUElTM0U0NEtoTXFLakt0YmtCZThIOEdiaXVvQVhBWURSb1ZSSG9ka1l3UkFJZ2FnR2tYdG93VXliZGx0S29qdjBsdnZmbHJsUTlJUm5QT2pla0Y2MGlIemdDSUNnNlpvY0lNemtVdXZPOTFCRW9ybUlXbVg0Ry9NR1QyenJvNkkvUHZCOFhjQkpBY0pMWGt0L3cva2N3RXZOaVptaTJpMm5NbjF3aVAzTFM5TkpqQlBqdThLRkxBTWcwTzl5ZFFUNjdVL0FMWU9lVFBUTzIvaTJZdzlPU2xpYnRxaGd6REE9PSJ9LHsidmFsaWRhdGlvbl9wdWJsaWNfa2V5IjoiRURDMjQ1MDI3QTUyRUU1MzE4MDk1NTk4RUMzQUI2NUZGNEEzQjlGOTQyOEUxMEIyRjNDNkYzOURFMTVBMTVDOTBBIiwibWFuaWZlc3QiOiJKQUFBQUFGeEllM0NSUUo2VXU1VEdBbFZtT3c2dGwvMG83bjVRbzRRc3ZQRzg1M2hXaFhKQ25NaEEvOC85cktVZEE2MWovZklFUC9jcUxweEJsbUloUDJyZzFkN05hRVB5S1YrZGtjd1JRSWhBSXhFME0vRko1MHZmWlc2ZlBweTR5Q1p1bVk5bjBvYnJPb2pVa2ptNTVhMEFpQmo1Nk8wTXBvcEdvWTlIeEMvKzR3Tk8zNkhvN0U5Q1FlSHNuS3JlRGRzQVhBU1FJWVVkODFqYmlWVWxFVDRkR29HMnArY2YrMkdxRVhYNWZKTVNTeVgvcWUwWGZSNGNPKzRxbGdtak1RZENSREJXQUJIVnZkTi95WnlpL3JMMmMrV3JRYz0ifSx7InZhbGlkYXRpb25fcHVibGljX2tleSI6IkVENDI0NkFBM0FFOUQyOTg2Mzk0NDgwMENDQTkxODI5RTQ0NDc0OThBMjBDRDlDMzk3M0E2QjU5MzQ2Qzc1QUI5NSIsIm1hbmlmZXN0IjoiSkFBQUFBRnhJZTFDUnFvNjZkS1lZNVJJQU15cEdDbmtSSFNZb2d6Wnc1YzZhMWswYkhXcmxYTWhBa20xbHowYzhRWFdmSjliMXZCNzJkTGFidzh3WUlkOE10bnBzSEhCRUM4cGRrWXdSQUlnUWxiNkhKNTNoc1RBZlZpZCtBT2RCVnZNRjdyYWhJS05MQkhVZ241MnpCRUNJR0xVcUZ1OGExQUFIUkpjVm9uS1lFbm1oSndiQ1hMbitqZTduYTFXRDEvb2NCSkFFNHZmdnJHU21aQzJ1QVVHbU01ZElCdG9TZ0VVZXkrMlZsZURZRXNjZTk0dHhZY2pSOFo3UUxOYWxpRDh3L2JENS9odllROG1lVjFXZzFqSkZOZTBDQT09In0seyJ2YWxpZGF0aW9uX3B1YmxpY19rZXkiOiJFRDJDMTQ2OEI0QTExRDI4MUY5M0VGMzM3Qzk1RTRBMDhERjAwMDBGREVGQjZEMEVBOUJDMDVGQkQ1RDYxQTFGNUEiLCJtYW5pZmVzdCI6IkpBQUFBQUZ4SWUwc0ZHaTBvUjBvSDVQdk0zeVY1S0NOOEFBUDN2dHREcW04QmZ2VjFob2ZXbk1oQWtNVW1DRDJhUG1nRkREUm1pbXZTaWNTSVNjdzZZTnI0MkR3NFJBZHdyT0Fka2N3UlFJaEFKRk9ITWc2cVRHOHY2MGRocmVuWVlrNmN3T2FSWHEwUk5tTGp5eUNpejVsQWlBZFUwWWtEVUpRaG5OOFJ5OHMrNnpUSkxpTkxidE04b08vY0xudXJWcFJNM0FTUUdBTGFySEFzSmtTWlF0R2RNMkFhUi9qb0ZLL2poRFU1NytsK1JTWWpyaS95ZEUyMERhS2Fud2tNRW9WbEJUZzdsWDRoWWpFbm1rcW83M3dJdGhMT0FRPSJ9LHsidmFsaWRhdGlvbl9wdWJsaWNfa2V5IjoiRURBNTRDODVGOTEyMTlGRDI1OTEzNEI2QjEyNkFENjRBRTcyMDRCODFERDQwNTI1MTA2NTdFMUE1Njk3MjQ2QUQyIiwibWFuaWZlc3QiOiJKQUFBQUFKeEllMmxUSVg1RWhuOUpaRTB0ckVtcldTdWNnUzRIZFFGSlJCbGZocFdseVJxMG5NaEFsOGNKZXJQdit2bzFCSzYxMXhWVHBHeGpqci9DdXhQVGdVOFVSTTRlVFo1ZGtZd1JBSWdkSzNjUVYyWS92aVpuZS9QYm9LU0tld25nVHVJTjJNNmM4YXp3cWMyMHVVQ0lBYzZHb05UK1AyWUJ5NDlnZGF1NFA3eVN3V29RWDVuZjlkUXhpUWF2NXdJY0JKQXFpQ0swZDZRUlpTcGlWSHA4TzlubEtYQ1NFaHNpU05jV2NFRm0vZkdoSkFuQU4wT3Y5SElOSWQxcHhyQm4yZEtSZWdMVHZZRzNCcGJ6Ly9ITGdFZERBPT0ifSx7InZhbGlkYXRpb25fcHVibGljX2tleSI6IkVEOUFFNEY1ODg3QkEwMjlFQjdDMDg4NDQ4NkQyM0NGMjgxOTc1Rjc3M0Y0NEJEMjEzMDU0MjE5ODgyQzQxMUNDNyIsIm1hbmlmZXN0IjoiSkFBQUFBRnhJZTJhNVBXSWU2QXA2M3dJaEVodEk4OG9HWFgzYy9STDBoTUZRaG1JTEVFY3gzTWhBbUcyemd2OEZCWnNaSlU4YVBhcHdvOWNJcVF2NC9NU1Mxb1ZBNWVWTWl3TGRrWXdSQUlnRitMT2U0ZVkwZ3A5dHRxaDJnbnYrejc1T3FMeU9RTXBHUEFMZ20rTnRPc0NJQ0RYQlpWUHRwcm1CRGtCSmtQRlNuRTU1RDllS1lSSDh6L2lZMUV0cE5wbGNCSkFBREVXR1ZUODBPd2hkMWxoMkpzVS9vWmxtZU5GNVdON1l2bEI4bGxFeGFSS0VWQytHVzlXZytpTklRM3JtVjdQOGFOYVZ1YWFiRzAwZk9na2d6TmhEdz09In0seyJ2YWxpZGF0aW9uX3B1YmxpY19rZXkiOiJFREE4RDI5RjQwQ0VCMjg5OTU2MTc2NDFBM0JDNDI2OTJFMURFODgzMjE0RjYxMkZCQjYyMDg3QTE0OEU1RjZGOUEiLCJtYW5pZmVzdCI6IkpBQUFBQUZ4SWUybzBwOUF6cktKbFdGMlFhTzhRbWt1SGVpRElVOWhMN3RpQ0hvVWpsOXZtbk1oQW5ZblA3RWc2VmdObkVVVFJFMjlkNjRqUVQvaUJjV1RRdE5yVXp5RDZNSitka2N3UlFJaEFPRXNWNWFuVGtsb1NtVFpSYmltTXlCS3FIb0pZWGNCQmU4bExpUFlDN21VQWlBejJhTk9wZlEvMUx5Y1dsb0lNdmRoeHppbnE1WDdVYXMvdU9TYjl3aDhkM0FTUUxWa2ZwVy9HTzZ3ZFQ2QXV1U0o1NlR0TTM0M3BETkgraVN6eGx0SWZkclBpVXhUNXJmNGsyMWxRUXVQQ2xYbTkrU2ZLckNpVVhaSzdkajAvR1dUWVFnPSJ9LHsidmFsaWRhdGlvbl9wdWJsaWNfa2V5IjoiRUQzOEIwMjg4RUEyNDBCNENERUMxOEExQTYyODlFQjQ5MDA3RTRFQkMwREU5NDQ4MDNFQjdFRjE0MUM1NjY0MDczIiwibWFuaWZlc3QiOiJKQUFBQUFGeEllMDRzQ2lPb2tDMHpld1lvYVlvbnJTUUIrVHJ3TjZVU0FQcmZ2RkJ4V1pBYzNNaEFnT0tjdkl1Y2hhbHJady9nbFR1T3hWM0lPQ2Nwb3J4TUI3SnFBVnVwazFlZGtjd1JRSWhBT3ZSenBlK0lZWksxTXlJbklRWjg3SnZQMko4U0lYQ1haTVBCQ2RJVEJhbUFpQVNhdkpYaTlwd3M4ckRESlN4aEdNbG1FN3pJNWJTQThpdnRSQzlMZ3ErVVhBU1FEbDNlb3FMSUQrRVRKTk0remJNdXZ3dmNIRUl4ZUJaa1o5ZnA1akp2Nk9DVFB3bGo0VEpTdXkxYXZFV3FVWVMycml2NUR2bDJoYUZVb0NIZjR5YXdBQT0ifSx7InZhbGlkYXRpb25fcHVibGljX2tleSI6IkVERUUxMERDMzZBQ0Q5OTVDOEUwRTg2RTNDRDJGQkY4MzAxQTRBQzJCODg0N0I2MUExOTM1REU0OTczQjQwN0MwRSIsIm1hbmlmZXN0IjoiSkFBQUFBRnhJZTN1RU53MnJObVZ5T0RvYmp6UysvZ3dHa3JDdUlSN1lhR1RYZVNYTzBCOERuTWhBbVgwdmI3aitsZ0JqRmpiTjlSbEE4Nko3QU8yVm42SExxdU8zYWlzSzRtd2RrWXdSQUlnZnhCTG43aTRqZy9kaTBVMjVxNmtJYlZmVHpxYkEwU0NwUTBJNTdUT0ZrY0NJRk10SlFwRU5qQjJLMkVtdkJIUHZOY3d1U1BjM3ZzRWVxRTJyTkovY1Q1RGNCSkFmNjhYUEZ1NVJqQ2VMZ3BGSk03UEtGTGdvVjhlMW54TzVld2pxOVErVEFFR25GeVMwSU93ZjZwT090SVZNZFZlWHUxdjZwNGZoWFFrZGloSHQxeDZBZz09In0seyJ2YWxpZGF0aW9uX3B1YmxpY19rZXkiOiJFRDU4M0VDRDA2QzNCNzM2OTk4MEU2NUM3OEM0NDBBNTI5MzAwRjU1N0VEODEyNTYyODNGN0RENUFBMzUxM0EzMzQiLCJtYW5pZmVzdCI6IkpBQUFBQUZ4SWUxWVBzMEd3N2MybVlEbVhIakVRS1VwTUE5VmZ0Z1NWaWcvZmRXcU5ST2pOSE1oQXl1VW56WloxbjIvR2FUbUUxbTdIL3Y5WWxaeURFd0hZM2dTSFVBM0lDTDlka1l3UkFJZ0h4MlBIdmlkb04rNXlHOVdlQVMyazdud0lNOGFqeFFXNndqdnQ4a0JlbkFDSUROeFFQUWtEeURKSDlzZVM1QzYybUFhclFtZ2lOODlZUzNqaE50bnZFSXFjQkpBajdKaDBLYWMrYUpkcG9lcHUvK2VKS25uRlE3WUJ5WkI4ZU1aK1NTMXpMaEUrbGlwLzQ5cXFWTmNwQXhFcWZhR3R4SnpvREREMS9RYnVVN05PU1BrQ2c9PSJ9LHsidmFsaWRhdGlvbl9wdWJsaWNfa2V5IjoiRUQ5NUM1MTcyQjJBRDdEMzk0MzRFRUJDNDM2QjY1QjNCQjdFNThENUMxQ0VGQzgyMEI2OTcyQUNBRDc3NkUyODZBIiwibWFuaWZlc3QiOiJKQUFBQUFGeEllMlZ4UmNyS3RmVGxEVHV2RU5yWmJPN2ZsalZ3Yzc4Z2d0cGNxeXRkMjRvYW5NaEFpcWNSZGUzTVFaMDc1ZmE0Wk5OeVJhWUpHTWRCTmtCbm4zYlFyS3NlQkRRZGtZd1JBSWdVK0xmY0U3MURQVnJPK0t0VUJqUTlEMnUway9QcjdsdWtPMW5QUmo2aFNBQ0lETkxZQy9KRmdvYkNzSWEwQkd3KzZiVW5PdzltZVUzRmRYZ1I3UTdTb3FKY0JKQVhRYWtPb1FuUHAzcGNMTDd6ZEtDUFVYNGIrL0ZDOVVuaHFwK085eFFGblJhQ1dWR21rNU1KT0lNczRXT1FkcE0xajNPZ1NzQUJtUnVDWFl2d28vbkR3PT0ifSx7InZhbGlkYXRpb25fcHVibGljX2tleSI6IkVEOTAxNjNEMkJGMEI3Nzg4OTA0QzRBNDExOEQ3RDk2ODkyMEU4NDdEODhCNzkxNzgzOTA4MzdERTNDQTI2MTU2MiIsIm1hbmlmZXN0IjoiSkFBQUFBRnhJZTJRRmowcjhMZDRpUVRFcEJHTmZaYUpJT2hIMkl0NUY0T1FnMzNqeWlZVlluTWhBNzJWVFJpR2hrSkJ0cWdHSER6SGo3WWJDNitOc0VLckZITnVFL0xPM1RuNWRrWXdSQUlnZjhzK2ZZdDBsbHJLUTJxaVdQbkdtYjZxSlBvZThPbkNNM1ZTMjlYS2JZWUNJSEdubEo0T1RzMmRYdWdPNkJ0bzYzTnBEdnZxSitXSXdkWUtxWjZCaUJmemNCSkFHdk50a29nNHBmRTVkWlJ3bWljODdaQmVldW5PaDRZcEwwU0VSZHhXajQzQ3M5ODE1ekZKdVp5c1NhVVgyUi92ZEUyVktxdlNncXF0REVuck1vMm9Bdz09In1dfQ==",
        "signature" : "9FF30EDC4DED7ABCD0D36389B7C716EED4B5E4F043902853534EBAC7BE966BB3813D5CF25E4DADA5E657CCF019FFD11847FD3CC44B5559A6FCEEE4C3DCFF8D0E",
        "version": 1
}
)vl";
}

auto constexpr default_expires = std::chrono::seconds{3600};
auto constexpr default_effective_overlap = std::chrono::seconds{30};
}  // namespace detail

class ValidatorSite_test : public beast::unit_test::suite
{
private:
    using Validator = TrustedPublisherServer::Validator;

    void
    testConfigLoad()
    {
        testcase("Config Load");

        using namespace jtx;

        Env env(*this, envconfig(), nullptr, beast::severities::kDisabled);
        auto trustedSites =
            std::make_unique<ValidatorSite>(env.app(), env.journal);

        // load should accept empty sites list
        std::vector<std::string> emptyCfgSites;
        BEAST_EXPECT(trustedSites->load(emptyCfgSites));

        // load should accept valid validator site uris
        std::vector<std::string> cfgSites({
            "http://ripple.com/", "http://ripple.com/validators",
                "http://ripple.com:8080/validators",
                "http://207.261.33.37/validators",
                "http://207.261.33.37:8080/validators",
                "https://ripple.com/validators",
                "https://ripple.com:443/validators",
                "file:///etc/opt/ripple/validators.txt",
                "file:///C:/Lib/validators.txt"
#if !_MSC_VER
                ,
                "file:///"
#endif
        });
        BEAST_EXPECT(trustedSites->load(cfgSites));

        // load should reject validator site uris with invalid schemes
        std::vector<std::string> badSites({"ftp://ripple.com/validators"});
        BEAST_EXPECT(!trustedSites->load(badSites));

        badSites[0] = "wss://ripple.com/validators";
        BEAST_EXPECT(!trustedSites->load(badSites));

        badSites[0] = "ripple.com/validators";
        BEAST_EXPECT(!trustedSites->load(badSites));

        // Host names are not supported for file URLs
        badSites[0] = "file://ripple.com/vl.txt";
        BEAST_EXPECT(!trustedSites->load(badSites));

        // Even local host names are not supported for file URLs
        badSites[0] = "file://localhost/home/user/vl.txt";
        BEAST_EXPECT(!trustedSites->load(badSites));

        // Nor IP addresses
        badSites[0] = "file://127.0.0.1/home/user/vl.txt";
        BEAST_EXPECT(!trustedSites->load(badSites));

        // File URL path can not be empty
        badSites[0] = "file://";
        BEAST_EXPECT(!trustedSites->load(badSites));

#if _MSC_VER  // Windows paths strip off the leading /, leaving the path empty
        // File URL path can not be a directory
        // (/ is the only path we can reasonably assume is a directory)
        badSites[0] = "file:///";
        BEAST_EXPECT(!trustedSites->load(badSites));
#endif
    }

    struct FetchListConfig
    {
        std::string path;
        std::string msg;
        bool ssl;
        bool failFetch = false;
        bool failApply = false;
        int serverVersion = 1;
        std::chrono::seconds expiresFromNow = detail::default_expires;
        std::chrono::seconds effectiveOverlap =
            detail::default_effective_overlap;
        int expectedRefreshMin = 0;
    };
    void
    testFetchList(
        detail::DirGuard const& good,
        std::vector<FetchListConfig> const& paths)
    {
        testcase << "Fetch list - "
                 << boost::algorithm::join(
                        paths |
                            boost::adaptors::transformed(
                                [](FetchListConfig const& cfg) {
                                    return cfg.path +
                                        (cfg.ssl ? " [https] v" : " [http] v") +
                                        std::to_string(cfg.serverVersion) +
                                        " " + cfg.msg;
                                }),
                        ", ");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env(*this, [&]() {
            auto p = test::jtx::envconfig();
            p->legacy("database_path", good.subdir().string());
            return p;
        }());
        auto& trustedKeys = env.app().validators();
        env.timeKeeper().set(env.timeKeeper().now() + 30s);

        test::StreamSink sink;
        beast::Journal journal{sink};

        std::vector<std::string> emptyCfgKeys;
        struct publisher
        {
            publisher(FetchListConfig const& c) : cfg{c}
            {
            }
            std::shared_ptr<TrustedPublisherServer> server;
            std::vector<Validator> list;
            std::string uri;
            FetchListConfig const& cfg;
            bool isRetry;
        };
        std::vector<publisher> servers;

        auto constexpr listSize = 20;
        std::vector<std::string> cfgPublishers;

        for (auto const& cfg : paths)
        {
            servers.push_back(cfg);
            auto& item = servers.back();
            item.isRetry = cfg.path == "/bad-resource";
            item.list.reserve(listSize);
            while (item.list.size() < listSize)
                item.list.push_back(TrustedPublisherServer::randomValidator());

            NetClock::time_point const expires =
                env.timeKeeper().now() + cfg.expiresFromNow;
            NetClock::time_point const effective2 =
                expires - cfg.effectiveOverlap;
            NetClock::time_point const expires2 =
                effective2 + cfg.expiresFromNow;
            item.server = make_TrustedPublisherServer(
                env.app().getIOService(),
                item.list,
                expires,
                {{effective2, expires2}},
                cfg.ssl,
                cfg.serverVersion);
            std::string pubHex = strHex(item.server->publisherPublic());
            cfgPublishers.push_back(pubHex);

            if (item.cfg.failFetch)
            {
                // Create a cache file
                auto const name = good.subdir() / ("cache." + pubHex);
                std::ofstream o(name.string());
                o << "{}";
            }

            std::stringstream uri;
            uri << (cfg.ssl ? "https://" : "http://")
                << item.server->local_endpoint() << cfg.path;
            item.uri = uri.str();
        }

        BEAST_EXPECT(trustedKeys.load({}, emptyCfgKeys, cfgPublishers));

        // Normally, tests will only need a fraction of this time,
        // but sometimes DNS resolution takes an inordinate amount
        // of time, so the test will just wait.
        auto sites = std::make_unique<ValidatorSite>(env.app(), journal, 12s);

        std::vector<std::string> uris;
        for (auto const& u : servers)
        {
            log << "Testing " << u.uri << std::endl;
            uris.push_back(u.uri);
        }
        sites->load(uris);
        sites->start();
        sites->join();

        auto const jv = sites->getJson();
        for (auto const& u : servers)
        {
            for (auto const& val : u.list)
            {
                BEAST_EXPECT(
                    trustedKeys.listed(val.masterPublic) != u.cfg.failApply);
                BEAST_EXPECT(
                    trustedKeys.listed(val.signingPublic) != u.cfg.failApply);
            }

            Json::Value myStatus;
            for (auto const& vs : jv[jss::validator_sites])
                if (vs[jss::uri].asString().find(u.uri) != std::string::npos)
                    myStatus = vs;
            BEAST_EXPECTS(
                myStatus[jss::last_refresh_message].asString().empty() !=
                    u.cfg.failFetch,
                to_string(myStatus) + "\n" + sink.messages().str());

            if (!u.cfg.msg.empty())
            {
                BEAST_EXPECTS(
                    sink.messages().str().find(u.cfg.msg) != std::string::npos,
                    sink.messages().str());
            }

            if (u.cfg.expectedRefreshMin)
            {
                BEAST_EXPECTS(
                    myStatus[jss::refresh_interval_min].asInt() ==
                        u.cfg.expectedRefreshMin,
                    to_string(myStatus));
            }

            if (u.cfg.failFetch)
            {
                using namespace std::chrono;
                std::stringstream nextRefreshStr{
                    myStatus[jss::next_refresh_time].asString()};
                system_clock::time_point nextRefresh;
                date::from_stream(nextRefreshStr, "%Y-%b-%d %T", nextRefresh);
                BEAST_EXPECT(!nextRefreshStr.fail());
                auto now = system_clock::now();
                BEAST_EXPECTS(
                    nextRefresh <= now + (u.isRetry ? seconds{30} : minutes{5}),
                    "Now: " + to_string(now) + ", NR: " + nextRefreshStr.str());
            }
        }
    }

    void
    testFileList(std::vector<std::pair<std::string, std::string>> const& paths)
    {
        testcase << "File list - " << paths[0].first
                 << (paths.size() > 1 ? ", " + paths[1].first : "");

        using namespace jtx;

        Env env(*this);

        test::StreamSink sink;
        beast::Journal journal{sink};

        struct publisher
        {
            std::string uri;
            std::string expectMsg;
            bool shouldFail;
        };
        std::vector<publisher> servers;

        for (auto const& cfg : paths)
        {
            servers.push_back({});
            auto& item = servers.back();
            item.shouldFail = !cfg.second.empty();
            item.expectMsg = cfg.second;

            std::stringstream uri;
            uri << "file://" << cfg.first;
            item.uri = uri.str();
        }

        auto sites = std::make_unique<ValidatorSite>(env.app(), journal);

        std::vector<std::string> uris;
        for (auto const& u : servers)
            uris.push_back(u.uri);
        sites->load(uris);
        sites->start();
        sites->join();

        for (auto const& u : servers)
        {
            auto const jv = sites->getJson();
            Json::Value myStatus;
            for (auto const& vs : jv[jss::validator_sites])
                if (vs[jss::uri].asString().find(u.uri) != std::string::npos)
                    myStatus = vs;
            BEAST_EXPECTS(
                myStatus[jss::last_refresh_message].asString().empty() !=
                    u.shouldFail,
                to_string(myStatus));
            if (u.shouldFail)
            {
                BEAST_EXPECTS(
                    sink.messages().str().find(u.expectMsg) !=
                        std::string::npos,
                    sink.messages().str());
            }
        }
    }

    void
    testFileURLs()
    {
        auto fullPath = [](detail::FileDirGuard const& guard) {
            auto absPath = absolute(guard.file()).string();
            if (absPath.front() != '/')
                absPath.insert(absPath.begin(), '/');
            return absPath;
        };
        {
            // Create a file with a real validator list
            detail::FileDirGuard good(
                *this, "test_val", "vl.txt", detail::realValidatorContents());
            // Create a file with arbitrary content
            detail::FileDirGuard hello(
                *this, "test_val", "helloworld.txt", "Hello, world!");
            // Create a file with malformed Json
            detail::FileDirGuard json(
                *this,
                "test_val",
                "json.txt",
                R"json({ "version": 2, "extra" : "value" })json");
            auto const goodPath = fullPath(good);
            auto const helloPath = fullPath(hello);
            auto const jsonPath = fullPath(json);
            auto const missingPath = jsonPath + ".bad";
            testFileList({
                {goodPath, ""},
                {helloPath,
                 "Unable to parse JSON response from  file://" + helloPath},
                {jsonPath,
                 "Missing fields in JSON response from  file://" + jsonPath},
                {missingPath, "Problem retrieving from file://" + missingPath},
            });
        }
    }

public:
    void
    run() override
    {
        testConfigLoad();

        detail::DirGuard good(*this, "test_fetch");
        for (auto ssl : {true, false})
        {
            // fetch single site
            testFetchList(good, {{"/validators", "", ssl}});
            testFetchList(good, {{"/validators2", "", ssl}});
            // fetch multiple sites
            testFetchList(
                good, {{"/validators", "", ssl}, {"/validators", "", ssl}});
            testFetchList(
                good, {{"/validators", "", ssl}, {"/validators2", "", ssl}});
            testFetchList(
                good, {{"/validators2", "", ssl}, {"/validators", "", ssl}});
            testFetchList(
                good, {{"/validators2", "", ssl}, {"/validators2", "", ssl}});
            // fetch single site with single redirects
            testFetchList(good, {{"/redirect_once/301", "", ssl}});
            testFetchList(good, {{"/redirect_once/302", "", ssl}});
            testFetchList(good, {{"/redirect_once/307", "", ssl}});
            testFetchList(good, {{"/redirect_once/308", "", ssl}});
            // one redirect, one not
            testFetchList(
                good,
                {{"/validators", "", ssl}, {"/redirect_once/302", "", ssl}});
            testFetchList(
                good,
                {{"/validators2", "", ssl}, {"/redirect_once/302", "", ssl}});
            // UNLs with a "gap" between validUntil of one and validFrom of the
            // next
            testFetchList(
                good,
                {{"/validators2",
                  "",
                  ssl,
                  false,
                  false,
                  1,
                  detail::default_expires,
                  std::chrono::seconds{-90}}});
            // fetch single site with undending redirect (fails to load)
            testFetchList(
                good,
                {{"/redirect_forever/301",
                  "Exceeded max redirects",
                  ssl,
                  true,
                  true}});
            // two that redirect forever
            testFetchList(
                good,
                {{"/redirect_forever/307",
                  "Exceeded max redirects",
                  ssl,
                  true,
                  true},
                 {"/redirect_forever/308",
                  "Exceeded max redirects",
                  ssl,
                  true,
                  true}});
            // one undending redirect, one not
            testFetchList(
                good,
                {{"/validators", "", ssl},
                 {"/redirect_forever/302",
                  "Exceeded max redirects",
                  ssl,
                  true,
                  true}});
            // one undending redirect, one not
            testFetchList(
                good,
                {{"/validators2", "", ssl},
                 {"/redirect_forever/302",
                  "Exceeded max redirects",
                  ssl,
                  true,
                  true}});
            // invalid redir Location
            testFetchList(
                good,
                {{"/redirect_to/ftp://invalid-url/302",
                  "Invalid redirect location",
                  ssl,
                  true,
                  true}});
            testFetchList(
                good,
                {{"/redirect_to/file://invalid-url/302",
                  "Invalid redirect location",
                  ssl,
                  true,
                  true}});
            // invalid json
            testFetchList(
                good,
                {{"/validators/bad",
                  "Unable to parse JSON response",
                  ssl,
                  true,
                  true}});
            testFetchList(
                good,
                {{"/validators2/bad",
                  "Unable to parse JSON response",
                  ssl,
                  true,
                  true}});
            // error status returned
            testFetchList(
                good,
                {{"/bad-resource", "returned bad status", ssl, true, true}});
            // location field missing
            testFetchList(
                good,
                {{"/redirect_nolo/308",
                  "returned a redirect with no Location",
                  ssl,
                  true,
                  true}});
            // json fields missing
            testFetchList(
                good,
                {{"/validators/missing",
                  "Missing fields in JSON response",
                  ssl,
                  true,
                  true}});
            testFetchList(
                good,
                {{"/validators2/missing",
                  "Missing fields in JSON response",
                  ssl,
                  true,
                  true}});
            // timeout
            testFetchList(
                good, {{"/sleep/13", "took too long", ssl, true, true}});
            // bad manifest format using known versions
            // * Retrieves a v1 formatted list claiming version 2
            testFetchList(
                good, {{"/validators", "Missing fields", ssl, true, true, 2}});
            // * Retrieves a v2 formatted list claiming version 1
            testFetchList(
                good, {{"/validators2", "Missing fields", ssl, true, true, 0}});
            // bad manifest version
            // Because versions other than 1 are treated as v2, the v1
            // list won't have the blobs_v2 fields, and thus will claim to have
            // missing fields
            testFetchList(
                good, {{"/validators", "Missing fields", ssl, true, true, 4}});
            testFetchList(
                good,
                {{"/validators2",
                  "1 unsupported version",
                  ssl,
                  false,
                  true,
                  4}});
            using namespace std::chrono_literals;
            // get expired validator list
            testFetchList(
                good,
                {{"/validators",
                  "Applied 1 expired validator list(s)",
                  ssl,
                  false,
                  false,
                  1,
                  0s}});
            testFetchList(
                good,
                {{"/validators2",
                  "Applied 1 expired validator list(s)",
                  ssl,
                  false,
                  false,
                  1,
                  0s,
                  -1s}});
            // force an out-of-range validUntil value
            testFetchList(
                good,
                {{"/validators",
                  "1 invalid validator list(s)",
                  ssl,
                  false,
                  true,
                  1,
                  std::chrono::seconds{Json::Value::maxInt + 1}}});
            // force an out-of-range validUntil value on the future list
            // The first list is accepted. The second fails. The parser
            // returns the "best" result, so this looks like a success.
            testFetchList(
                good,
                {{"/validators2",
                  "",
                  ssl,
                  false,
                  false,
                  1,
                  std::chrono::seconds{Json::Value::maxInt - 300},
                  299s}});
            // force an out-of-range validFrom value
            // The first list is accepted. The second fails. The parser
            // returns the "best" result, so this looks like a success.
            testFetchList(
                good,
                {{"/validators2",
                  "",
                  ssl,
                  false,
                  false,
                  1,
                  std::chrono::seconds{Json::Value::maxInt - 300},
                  301s}});
            // force an out-of-range validUntil value on _both_ lists
            testFetchList(
                good,
                {{"/validators2",
                  "2 invalid validator list(s)",
                  ssl,
                  false,
                  true,
                  1,
                  std::chrono::seconds{Json::Value::maxInt + 1},
                  std::chrono::seconds{Json::Value::maxInt - 6000}}});
            // verify refresh intervals are properly clamped
            testFetchList(
                good,
                {{"/validators/refresh/0",
                  "",
                  ssl,
                  false,
                  false,
                  1,
                  detail::default_expires,
                  detail::default_effective_overlap,
                  1}});  // minimum of 1 minute
            testFetchList(
                good,
                {{"/validators2/refresh/0",
                  "",
                  ssl,
                  false,
                  false,
                  1,
                  detail::default_expires,
                  detail::default_effective_overlap,
                  1}});  // minimum of 1 minute
            testFetchList(
                good,
                {{"/validators/refresh/10",
                  "",
                  ssl,
                  false,
                  false,
                  1,
                  detail::default_expires,
                  detail::default_effective_overlap,
                  10}});  // 10 minutes is fine
            testFetchList(
                good,
                {{"/validators2/refresh/10",
                  "",
                  ssl,
                  false,
                  false,
                  1,
                  detail::default_expires,
                  detail::default_effective_overlap,
                  10}});  // 10 minutes is fine
            testFetchList(
                good,
                {{"/validators/refresh/2000",
                  "",
                  ssl,
                  false,
                  false,
                  1,
                  detail::default_expires,
                  detail::default_effective_overlap,
                  60 * 24}});  // max of 24 hours
            testFetchList(
                good,
                {{"/validators2/refresh/2000",
                  "",
                  ssl,
                  false,
                  false,
                  1,
                  detail::default_expires,
                  detail::default_effective_overlap,
                  60 * 24}});  // max of 24 hours
        }
        using namespace boost::filesystem;
        for (auto const& file : directory_iterator(good.subdir()))
        {
            remove_all(file);
        }

        testFileURLs();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(ValidatorSite, app, ripple, 2);

}  // namespace test
}  // namespace ripple
