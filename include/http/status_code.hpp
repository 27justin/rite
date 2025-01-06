#pragma once

#define CODE(num, text) e##num = num, e##text = num
#define CODE_UNUSED(num) e##num = num

enum class http_status_code {
    // 1xx family
    CODE(100, Continue),
    CODE(101, SwitchingProtocols),
    CODE(102, Processing),
    CODE(103, EarlyHints),

    // 2xx family
    CODE(200, Ok),
    CODE(201, Created),
    CODE(202, Accepted),
    CODE(203, NonAuthorativeInformation),
    CODE(204, NoContent),
    CODE(205, ResetContent),
    CODE(206, PartialContent),
    CODE(207, MultiStatus),
    CODE(208, AlreadyReported),
    CODE(226, IMUsed),

    // 3xx family
    CODE(300, MultipleChoices),
    CODE(301, MovedPermanently),
    CODE(302, Found),
    CODE(303, SeeOther),
    CODE(304, NotModified),
    CODE(305, UseProxy),
    CODE_UNUSED(306),
    CODE(307, TemporaryRedirect),
    CODE(308, PermanentRedirect),

    // 4xx family
    CODE(400, BadRequest),
    CODE(401, Unauthorized),
    CODE(402, PaymentRequired),
    CODE(403, Forbidden),
    CODE(404, NotFound),
    CODE(405, MethodNotAllowed),
    CODE(406, NotAcceptable),
    CODE(407, ProxyAuthenticationRequired),
    CODE(408, RequestTimeout),
    CODE(409, Conflict),
    CODE(410, Gone),
    CODE(411, LengthRequired),
    CODE(412, PreconditionFailed),
    CODE(413, PayloadTooLarge),
    CODE(414, URITooLong),
    CODE(415, UnsupportedMediaType),
    CODE(416, RangeNotSatifiable),
    CODE(417, ExpectationFailed),
    CODE(421, MisdirectedRequest),
    CODE(422, UnprocessableEntity),
    CODE(423, Locked),
    CODE(424, FailedDependency),
    CODE(425, TooEarly),
    CODE(426, UpgradeRequired),
    CODE(428, PreconditionRequired),
    CODE(429, TooManyRequests),
    CODE(431, RequestHeaderFieldsTooLarge),
    CODE(451, UnavailableForLegalReasons),

    CODE(418, ImATeapot),

    // 5xx family
    CODE(500, InternalServerError),
    CODE(501, NotImplemented),
    CODE(502, BadGateway),
    CODE(503, ServiceUnavailable),
    CODE(504, GatewayTimeout),
    CODE(505, HTTPVersionNotSupported),
    CODE(506, VariantAlsoNegotiates),
    CODE(507, InsufficientStorage),
    CODE(508, LoopDetected),
    CODE(509, BandwidthLimitExceeded),
    CODE(510, NotExtended),
    CODE(511, NetworkAuthenticationRequired)
};
